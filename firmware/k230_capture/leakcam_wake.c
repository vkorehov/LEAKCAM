/*
 * leakcam_wake: what the K230 does at every wake. leakcam_agent runs it as its hook:
 *
 *   leakcam_wake <cold|leak|rtc|humid>
 *
 *   1. capture: both cameras, LEDs on for the shot, one frame each after AE/AWB settled;
 *   2. compare each frame, reduced to 320x240, with what the history shows (cam<N>.hist):
 *      imgdiff, and where it sees a change, the change net on the KPU (change.h) to drop
 *      changes of light only; nothing changed on any camera and no sensor alarm -> sleep,
 *      except once a day when a camera's light-only distance came within SAMPLE_MARGIN of its
 *      threshold: that pair is reported as a sample (sample=1), for the server to check the
 *      net. A sensor alarm (the wake came from the probes or the humidity alarm, or the probe
 *      node reads wet) is reported whatever the cameras see;
 *   3. something is new -> ask the agent for Wi-Fi ("wifi" on stdout, the answer "wifi=ok" or
 *      "wifi=fail,<code>" on stdin); no Wi-Fi, no server config or no connection -> sleep;
 *   4. POST /v1/check?reason=<reason>&probe_mv=<mV>&bat_mv=<mV>&rh=<%RH>&t=<C>&nn=<kmodel CRC>
 *      &thr=<cam0>,<cam1>&cam0=<state>,<distance>&cam1=...[&sample=1] (state first, same,
 *      changed or light; distance -1 where the net did not run). Body: per camera its reduced
 *      frame, then, where imgdiff saw a change, the history view it was compared with (PGMs one
 *      after the other), so the server also gets the pairs the net called light only: the
 *      training data for the next net (firmware/NN.txt). The server decides from the images and
 *      the sensors together and answers {"leak":true} or {"leak":false}, plus
 *      "nn":"<CRC>","thr":[<cam0>,<cam1>] when it has another net or other thresholds for this
 *      board; the new kmodel comes from GET /v1/nn/<CRC> and is kept by nnstore.h;
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
#include "change.h"
#include "history.h"
#include "imgdiff.h"
#include "led.h"
#include "netclient.h"
#include "nnstore.h"
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

#define SAMPLE_MARGIN  0.1f               /* a light-only distance this close to the threshold... */
#define SAMPLE_EVERY_S 86400              /* ...is reported at most once a day */

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
    bool have_view, changed, light;       /* light: imgdiff changed, the net said light only */
    float dist;                           /* the net's distance, -1 where it did not run */
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

/* the change net in use, loaded when first needed (a changed frame, or a report) */
static struct nn_model nn;
static int nn_state;                      /* 0 not tried, 1 loaded, -1 none */

static struct nn_model *nn_get(void)
{
    if (nn_state == 0) {
        nn_state = nnstore_load(STATE_DIR, CHANGE_KMODEL, &nn) == 0 &&
                   change_load(nn.data, nn.len) == 0 ? 1 : -1;
        if (nn_state > 0)
            printf("change net %08x, thresholds %.2f %.2f\n", (unsigned)nn.crc, nn.thr[0], nn.thr[1]);
    }
    return nn_state > 0 ? &nn : NULL;
}

static const char *state_name(const struct cam_frame *f)
{
    return !f->have_view ? "first" : f->changed ? "changed" : f->light ? "light" : "same";
}

/* the history view goes with the frame where imgdiff saw a change */
static bool send_view(const struct cam_frame *f)
{
    return f->have_view && f->diff.changed;
}

/* POST /v1/check: 1 leak, 0 no leak, -1 not reported; the server's answer in reply */
static int server_check(const char *host, const char *port, const char *reason, bool sample,
                        const struct sensors *s, const struct cam_frame *f, int n, char *reply, size_t cap)
{
    char head[32], path[320];
    int hlen = snprintf(head, sizeof(head), "P5\n%d %d\n255\n", IMGDIFF_W, IMGDIFF_H);
    char why[48];                         /* "rtc+leak": '+' is a space in a query, send %2B */
    size_t w = 0;
    for (const char *c = reason; *c && w + 4 < sizeof(why); c++)
        w += (size_t)snprintf(why + w, sizeof(why) - w, *c == '+' ? "%%2B" : "%c", *c);
    why[w] = 0;
    const struct nn_model *m = nn_get();
    int len = snprintf(path, sizeof(path), "/v1/check?reason=%s&probe_mv=%d&bat_mv=%d&rh=%s&t=%s&nn=%08x&thr=%.2f,%.2f",
                       why, s->probe_mv, s->bat_mv, s->rh, s->t, m ? (unsigned)m->crc : 0u,
                       m ? m->thr[0] : 0.0f, m ? m->thr[1] : 0.0f);
    long body = 0;
    for (int i = 0; i < n; i++) {
        len += snprintf(path + len, sizeof(path) - (size_t)len, "&cam%u=%s,%.3f", f[i].slot, state_name(&f[i]),
                        f[i].dist);
        body += (hlen + IMGDIFF_W * IMGDIFF_H) * (send_view(&f[i]) ? 2 : 1);
    }
    if (sample)
        snprintf(path + len, sizeof(path) - (size_t)len, "&sample=1");
    int fd = net_connect(host, port, NET_TIMEOUT_S);
    if (fd < 0)
        return -1;
    int rc = -1;
    if (http_post(fd, host, path, "image/x-portable-graymap", body) == 0) {
        bool sent = true;
        for (int i = 0; i < n && sent; i++) {
            sent = net_send_all(fd, head, (size_t)hlen) == 0 && net_send_all(fd, f[i].cur, sizeof(f[i].cur)) == 0;
            if (sent && send_view(&f[i]))
                sent = net_send_all(fd, head, (size_t)hlen) == 0 && net_send_all(fd, f[i].view, sizeof(f[i].view)) == 0;
        }
        int status = sent ? http_reply(fd, reply, cap) : -1;
        if (status == 200)
            rc = strstr(reply, "\"leak\":true") != NULL;
        else
            fprintf(stderr, "check: server status %d\n", status);
    }
    close(fd);
    return rc;
}

/* GET /v1/nn/<crc>: the kmodel, checked against crc; malloc'd, or NULL */
static uint8_t *nn_download(const char *host, const char *port, uint32_t crc, size_t *len_out)
{
    char path[32];
    snprintf(path, sizeof(path), "/v1/nn/%08x", (unsigned)crc);
    int fd = net_connect(host, port, NET_TIMEOUT_S);
    if (fd < 0)
        return NULL;
    long len = -1;
    uint8_t *p = NULL;
    int status = http_get(fd, host, path) == 0 ? http_reply_head(fd, &len) : -1;
    if (status == 200 && len > 0 && len <= (long)NN_MAX_LEN && (p = malloc((size_t)len)) != NULL &&
        refstore_read_all(fd, p, (size_t)len) == 0 && refstore_crc32(p, (size_t)len) == crc) {
        *len_out = (size_t)len;
    } else {
        fprintf(stderr, "nn: download %s failed (status %d, %ld bytes)\n", path, status, len);
        free(p);
        p = NULL;
    }
    close(fd);
    return p;
}

/* the server's "nn" and "thr" in reply: a new kmodel and/or thresholds, saved for the next wake */
static void nn_update(const char *host, const char *port, const char *reply)
{
    const char *a = strstr(reply, "\"nn\":\""), *b = strstr(reply, "\"thr\":[");
    unsigned crc;
    float thr[2];
    if (!a || !b || sscanf(a, "\"nn\":\"%8x\"", &crc) != 1 || sscanf(b, "\"thr\":[%f,%f]", &thr[0], &thr[1]) != 2)
        return;
    const struct nn_model *cur = nn_get();
    struct nn_model m = { NULL, 0, crc, { thr[0], thr[1] } };
    uint8_t *got = NULL;
    if (cur && cur->crc == crc) {         /* thresholds only: the same kmodel again */
        m.data = cur->data;
        m.len = cur->len;
    } else if ((got = nn_download(host, port, crc, &m.len)) != NULL) {
        m.data = got;
    } else {
        return;
    }
    if (nnstore_save(STATE_DIR, &m) == 0)
        printf("change net %08x, thresholds %.2f %.2f saved for the next wake\n", crc, thr[0], thr[1]);
    else
        fprintf(stderr, "nn: saving the update failed\n");
    free(got);
}

/* a light-only pair close to its threshold, and no sample sent for a day */
static bool sample_due(const struct cam_frame *f, int n)
{
    const struct nn_model *m = nn_get();
    bool near = false;
    for (int i = 0; m && i < n; i++)
        near |= f[i].light && f[i].dist >= m->thr[f[i].slot] - SAMPLE_MARGIN;
    if (!near)
        return false;
    long last = 0;
    FILE *fp = fopen(STATE_DIR "/sample", "r");
    if (fp) {
        if (fscanf(fp, "%ld", &last) != 1)
            last = 0;
        fclose(fp);
    }
    return time(NULL) - (time_t)last >= SAMPLE_EVERY_S;
}

static void sample_sent(void)
{
    FILE *fp = fopen(STATE_DIR "/sample", "w");
    if (fp) {
        fprintf(fp, "%ld\n", (long)time(NULL));
        fclose(fp);
    }
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
        f[i].dist = -1;
        if (f[i].have_view && f[i].changed) {
            /* imgdiff also trips on light; the change net tells a changed scene. Light only:
             * the view stays as it is, so a leak that grows is still measured against dry.
             * The net failed or missing: reported */
            const struct nn_model *m = nn_get();
            if (m)
                f[i].dist = change_distance(f[i].view, f[i].cur);
            printf("cam %u: change net %.3f\n", f[i].slot, f[i].dist);
            f[i].light = m && f[i].dist >= 0 && f[i].dist < m->thr[f[i].slot];
            f[i].changed = !f[i].light;
        }
        any |= f[i].changed;
        printf("cam %u: %s\n", f[i].slot, !f[i].have_view ? "first frame" : f[i].changed ? "changed" : "same");
    }
    bool sample = !any && !sensor_alarm && sample_due(f, n);
    if (!any && !sensor_alarm && !sample)
        return finish(SLEEP_NORMAL);
    const unsigned retry = sample ? SLEEP_NORMAL : SLEEP_RETRY;   /* a sample is not retried */
    if (sample)
        printf("sample: a light-only pair near its threshold\n");

    char host[64], port[8];
    if (net_server(STATE_DIR "/server", host, port) < 0) {
        fprintf(stderr, "no server configured in " STATE_DIR "/server\n");
        return finish(retry);
    }
    if (!wifi_up())
        return finish(retry);
    char reply[256];
    int leak = server_check(host, port, reason, sample, &sens, f, n, reply, sizeof(reply));
    if (leak < 0) {
        fprintf(stderr, "server not reached\n");
        return finish(retry);
    }
    if (sample)
        sample_sent();
    if (leak) {
        printf("leak reported\n");
        push_video(host);
        nn_update(host, port, reply);
        return finish(SLEEP_LEAK);
    }
    store(f, n);
    nn_update(host, port, reply);
    return finish(SLEEP_NORMAL);
}
