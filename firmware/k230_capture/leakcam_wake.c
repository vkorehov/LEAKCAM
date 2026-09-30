/*
 * leakcam_wake: what the K230 does at every wake. leakcam_agent runs it as its hook:
 *
 *   leakcam_wake <cold|leak|rtc|humid>
 *
 *   1. capture: both cameras, LEDs on for the shot, one frame each after AE/AWB settled;
 *   2. compare each frame, reduced to 320x240, with what the history shows (cam<N>.hist);
 *      nothing changed on any camera and no sensor alarm -> sleep. A sensor alarm (the wake
 *      came from the probes or the humidity alarm, or the probe node reads wet) is reported
 *      whatever the cameras see;
 *   3. something is new -> ask the agent for Wi-Fi ("wifi" on stdout, the answer "wifi=ok" or
 *      "wifi=fail,<code>" on stdin); no Wi-Fi, no server config or no connection -> sleep;
 *   4. POST /v1/check?reason=<reason>&probe_mv=<mV>&bat_mv=<mV>&rh=<%RH>&t=<C> with the
 *      reduced frame of every camera (PGM, one after the other); the server decides from the images and the
 *      sensors together and answers {"leak":true} or {"leak":false};
 *   5. leak -> leakcam_stream streams 5 s of video with audio from both cameras live over RTMP
 *      (rtmp://<server>:1935/leakcam/cam<N>), then sleep;
 *      the frames are not stored, so every wake reports again until the server says no leak;
 *   6. no leak -> the new frames go to the history (a delta of the changed blocks, or a
 *      keyframe) and sleep. Wi-Fi ends with the session: the BL616 stops it before power-off.
 *
 * The sensor values come from the BL616 (in WAKE) through the agent, always all four:
 * LEAKCAM_RH, LEAKCAM_T, LEAKCAM_PROBE_MV and LEAKCAM_BAT_MV (the K230's own ADC_1 reading). "sleep=<seconds>" on stdout picks the
 * next scheduled wake. The server address is one line
 * "<host> <port>" in /sdcard/leakcam/server. Nothing is kept in RAM between wakes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cap.h"
#include "history.h"
#include "imgdiff.h"
#include "led.h"
#include "netclient.h"
#include "refstore.h"

#ifndef STATE_DIR                         /* the host test points both elsewhere */
#define STATE_DIR      "/sdcard/leakcam"
#define STREAM_PROG    "/sdcard/app/leakcam_stream"
#endif
#define RTMP_PORT      "1935"             /* the server's RTMP ingest, on the host of /v1/check */
#define WIDTH          1280               /* OV5647 2x2 binned, full field of view */
#define HEIGHT         960
#define SETTLE_FRAMES  12                 /* ~0.3 s at 45 fps for AE/AWB */
#ifndef NET_TIMEOUT_S
#define NET_TIMEOUT_S  30                 /* association + DHCP after "wifi=ok" */
#endif
#define VIDEO_S        "5"

#define PROBE_WET_MV   825                /* the BL616 comparator's trip point (bl616/leak_wake.c) */

#define SLEEP_NORMAL   21600u             /* nothing new, or reported without a leak: 6 h */
#define SLEEP_RETRY    3600u              /* new, but not reported (no Wi-Fi or server): 1 h */
#define SLEEP_LEAK     600u               /* leak reported: look again in 10 min */

static unsigned led_percent[2] = { 100, 100 };

static int finish(unsigned sleep_s)
{
    printf("sleep=%u\n", sleep_s);
    return 0;
}

/* copy the changed blocks of the reduced frame into the reduced history view */
static void apply_blocks(uint8_t *view, const uint8_t *cur,
                         const uint8_t changed[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X])
{
    enum { BW = IMGDIFF_W / IMGDIFF_BLOCKS_X, BH = IMGDIFF_H / IMGDIFF_BLOCKS_Y };
    for (int by = 0; by < IMGDIFF_BLOCKS_Y; by++)
        for (int bx = 0; bx < IMGDIFF_BLOCKS_X; bx++)
            if (changed[by][bx])
                for (int y = by * BH; y < (by + 1) * BH; y++)
                    memcpy(view + y * IMGDIFF_W + bx * BW, cur + y * IMGDIFF_W + bx * BW, BW);
}

struct cam_frame {
    unsigned slot;
    uint8_t *luma;                        /* WIDTH x HEIGHT, kept for the history */
    uint8_t cur[IMGDIFF_W * IMGDIFF_H];   /* reduced */
    uint8_t view[IMGDIFF_W * IMGDIFF_H];  /* what the history shows */
    bool have_view, changed;
    struct imgdiff_result diff;
};

/* one frame per camera; the cameras are released again, leakcam_stream may need them */
static int capture(struct cam_frame *f, int n)
{
    struct cap_cam cams[CAP_MAX_CAMS];
    unsigned nodes[CAP_MAX_CAMS] = CAP_DEFAULT_NODES;
    memset(cams, 0, sizeof(cams));
    for (int i = 0; i < n; i++)
        cams[i].node = nodes[i];
    int rc = -1;
    if (cap_open_all(cams, n, WIDTH, HEIGHT) == 0) {
        if (led_set(true, led_percent, 0) < 0)
            fprintf(stderr, "led: continuing without full illumination\n");
        rc = cap_grab_all(cams, n, SETTLE_FRAMES, 5000);
        led_set(false, led_percent, 0);
    }
    for (int i = 0; rc == 0 && i < n; i++) {
        f[i].slot = cams[i].slot;
        f[i].luma = cams[i].luma;
        cams[i].luma = NULL;              /* ours now, cap_close_all() must not free it */
        if (imgdiff_reduce(f[i].luma, WIDTH, HEIGHT, WIDTH, f[i].cur) < 0)
            rc = -1;
    }
    cap_close_all(cams, n);
    return rc;
}

/* ask the agent for Wi-Fi and wait for its answer */
static bool wifi_up(void)
{
    char line[64];
    printf("wifi\n");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
        return false;
    if (strncmp(line, "wifi=ok", 7) != 0) {
        fprintf(stderr, "no Wi-Fi: %s", line);
        return false;
    }
    return true;
}

/* the BL616's values, as the agent passes them */
struct sensors {
    const char *rh, *t;                   /* %RH, C as the agent formats them */
    int probe_mv, bat_mv;
};

/* POST /v1/check: 1 leak, 0 no leak, -1 not reported */
static int server_check(const char *host, const char *port, const char *reason, const struct sensors *s,
                        const struct cam_frame *f, int n)
{
    char head[32], path[160], reply[256];
    int hlen = snprintf(head, sizeof(head), "P5\n%d %d\n255\n", IMGDIFF_W, IMGDIFF_H);
    char why[48];                         /* "rtc+leak": '+' is a space in a query, send %2B */
    size_t w = 0;
    for (const char *c = reason; *c && w + 4 < sizeof(why); c++)
        w += (size_t)snprintf(why + w, sizeof(why) - w, *c == '+' ? "%%2B" : "%c", *c);
    why[w] = 0;
    snprintf(path, sizeof(path), "/v1/check?reason=%s&probe_mv=%d&bat_mv=%d&rh=%s&t=%s", why,
             s->probe_mv, s->bat_mv, s->rh, s->t);
    int fd = net_connect(host, port, NET_TIMEOUT_S);
    if (fd < 0)
        return -1;
    int rc = -1;
    if (http_post(fd, host, path, "image/x-portable-graymap", (long)n * (hlen + IMGDIFF_W * IMGDIFF_H)) == 0) {
        bool sent = true;
        for (int i = 0; i < n && sent; i++)
            sent = net_send_all(fd, head, (size_t)hlen) == 0 && net_send_all(fd, f[i].cur, sizeof(f[i].cur)) == 0;
        int status = sent ? http_reply(fd, reply, sizeof(reply)) : -1;
        if (status == 200)
            rc = strstr(reply, "\"leak\":true") != NULL;
        else
            fprintf(stderr, "check: server status %d\n", status);
    }
    close(fd);
    return rc;
}

/* 5 s of both cameras live to the server's RTMP ingest, by leakcam_stream's push mode */
static void push_video(const char *host)
{
    char target[80];
    snprintf(target, sizeof(target), "%s:%s", host, RTMP_PORT);
    pid_t pid = fork();
    if (pid == 0) {
        execl(STREAM_PROG, STREAM_PROG, "-p", target, "-t", VIDEO_S, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        fprintf(stderr, "video push failed\n");
}

static void store(struct cam_frame *f, int n)
{
    struct hist_cfg hcfg;
    hist_default_cfg(&hcfg, STATE_DIR);
    time_t now = time(NULL);
    for (int i = 0; i < n; i++) {
        if (!f[i].changed)
            continue;
        struct hist_info hi;
        const uint8_t (*blocks)[IMGDIFF_BLOCKS_X] = f[i].have_view
            ? (const uint8_t (*)[IMGDIFF_BLOCKS_X])f[i].diff.block_changed : NULL;
        if (hist_store(&hcfg, (int)f[i].slot, now, f[i].luma, WIDTH, HEIGHT, blocks,
                       f[i].have_view ? f[i].diff.usable_blocks : 0, !f[i].have_view, &hi) < 0) {
            fprintf(stderr, "cam %u: storing history failed: %s\n", f[i].slot, strerror(errno));
            continue;
        }
        /* the view follows what was stored; written after the record, so a power cut in
         * between only makes the next wake store those blocks again */
        if (hi.kind == HIST_KEY)
            memcpy(f[i].view, f[i].cur, sizeof(f[i].view));
        else if (hi.kind == HIST_DELTA)
            apply_blocks(f[i].view, f[i].cur, (const uint8_t (*)[IMGDIFF_BLOCKS_X])f[i].diff.block_changed);
        if (hi.kind != HIST_NONE && refstore_save(STATE_DIR, (int)f[i].slot, f[i].view, now) < 0)
            fprintf(stderr, "cam %u: saving the history view failed: %s\n", f[i].slot, strerror(errno));
    }
}

int main(int argc, char **argv)
{
    const char *reason = argc > 1 ? argv[1] : "cold";
    static struct cam_frame f[2];
    const int n = 2;
    /* all four always set by the agent */
    struct sensors sens = { getenv("LEAKCAM_RH"), getenv("LEAKCAM_T"), atoi(getenv("LEAKCAM_PROBE_MV")),
                            atoi(getenv("LEAKCAM_BAT_MV")) };
    /* the reasons come as "rtc+leak+humid" (bl616 WAKE): any leak or humid part is an alarm */
    bool sensor_alarm = strstr(reason, "leak") || strstr(reason, "humid") ||
                        (sens.probe_mv >= 0 && sens.probe_mv < PROBE_WET_MV);
    printf("sensors: rh %s, t %s, probe %d mV, battery %d mV%s\n", sens.rh, sens.t, sens.probe_mv,
           sens.bat_mv, sensor_alarm ? ", alarm" : "");

    if (mkdir(STATE_DIR, 0755) < 0 && errno != EEXIST)
        return 1;
    if (capture(f, n) < 0) {
        fprintf(stderr, "capture failed\n");
        return finish(SLEEP_RETRY);
    }

    bool any = false;
    struct imgdiff_cfg cfg;
    imgdiff_default_cfg(&cfg);
    for (int i = 0; i < n; i++) {
        f[i].have_view = refstore_load(STATE_DIR, (int)f[i].slot, f[i].view, NULL) == 0;
        if (f[i].have_view)
            imgdiff_compare(f[i].view, f[i].cur, &cfg, &f[i].diff);
        f[i].changed = !f[i].have_view || f[i].diff.changed;   /* no history yet: new */
        any |= f[i].changed;
        printf("cam %u: %s\n", f[i].slot, !f[i].have_view ? "first frame" : f[i].changed ? "changed" : "same");
    }
    if (!any && !sensor_alarm)
        return finish(SLEEP_NORMAL);

    char host[64], port[8];
    if (net_server(STATE_DIR "/server", host, port) < 0) {
        fprintf(stderr, "no server configured in " STATE_DIR "/server\n");
        return finish(SLEEP_RETRY);
    }
    if (!wifi_up())
        return finish(SLEEP_RETRY);
    int leak = server_check(host, port, reason, &sens, f, n);
    if (leak < 0) {
        fprintf(stderr, "server not reached\n");
        return finish(SLEEP_RETRY);
    }
    if (leak) {
        printf("leak reported\n");
        push_video(host);
        return finish(SLEEP_LEAK);
    }
    store(f, n);
    return finish(SLEEP_NORMAL);
}
