/*
 * Camera focus (focus.h; DESIGN.md 5.3.17): the score, the reference and the bad-focus count; and the session over the
 * server (user 2026-11-13: "focus tuning is done on already setup phone", "live stream video", "toggle between two
 * cameras", "quickly switch camera and see instantly video changing"): media.c runs a capture of kind "focus" (the
 * app's Tune focus, handed over at the board's check-in) as focus_run(), which holds both cameras streaming
 * (job_focus: the LEDs on, the exposures held) and
 *   - uploads, chunked, POST /v1/focus/<dev>/stream: records [u32 BE length | 1 << 30 text][u64 BE us][payload]: per
 *     frame of the shown camera its H.265 access unit (the native 2592x1936 at 15 fps, H265_KBPS; the parameter sets
 *     with the session's first frame), CAM,<cam> before a camera's first one; once a second its whole-frame score
 *     FOC,<cam>,<seq>,<score> (5 of the 70 tiles scored per frame); FOCUS,OK,<cam>,<score> after a Done;
 *   - polls GET /v1/focus/<dev>/cmd (the server's long poll): {"cam":n} switches at the next frame (one encoder, fed
 *     only the shown camera, an IDR asked for: nothing restarts), {"done":s} keeps s as the camera's reference,
 *     {"stop":true} ends (the app left, nobody watching, or the session's time);
 * the server relays the records to the app (server/focus.go).
 */
#include "focus.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "k_video_comm.h"

#include "cap.h"
#include "job.h"
#include "h265.h"
#include "http_post.h"
#include "jpeg_hw.h"
#include "leakcam.h"

#define FILE_PATH   "/sdcard/leakcam/focus_ref" /* "<ref0> <ref1> <bad0> <bad1> <pct0> <pct1>"; was .../focus with the
                                         * 5-area score (before 2026-11-13): a reference of the old score ignored */
#define AREA        256
#define SESSION_MS  (10 * 60 * 1000)
#define QUEUE_MAX   (3u << 20)          /* encoded, not yet on the cable: past it frames drop up to the next IDR */

/* ---------------- the score ---------------- */

static double area(const uint8_t *y, unsigned stride, unsigned w, unsigned h, int cx, int cy)
{
    int x0 = cx - AREA / 2, y0 = cy - AREA / 2;
    if (x0 < 1 || y0 < 1 || x0 + AREA + 1 > (int)w || y0 + AREA + 1 > (int)h)
        return 0;
    uint64_t e = 0, sum = 0;
    int s = (int)stride;
    for (int r = 0; r < AREA; r++) {
        const uint8_t *p = y + (size_t)(y0 + r) * stride + x0;
        for (int x = 0; x < AREA; x++, p++) {
            int gx = p[-s + 1] + 2 * p[1] + p[s + 1] - p[-s - 1] - 2 * p[-1] - p[s - 1];
            int gy = p[s - 1] + 2 * p[s] + p[s + 1] - p[-s - 1] - 2 * p[-s] - p[-s + 1];
            e += (uint64_t)(gx * gx + gy * gy);
            sum += *p;
        }
    }
    double n = (double)AREA * AREA, m = (double)sum / n;
    return m < 4 ? 0 : (double)e / n / (m * m);
}

/* the tile grid: AREA squares side by side, centred, every one whole inside the image circle (imgdiff's 0.62 of the
 * width: 10 x 7 = 70 at 2592x1936, only the frame's very corners left out) */
static unsigned grid(unsigned w, unsigned h, int *x0, int *y0, unsigned *nx)
{
    *nx = w / AREA;
    unsigned ny = h / AREA;
    *x0 = (int)(w - *nx * AREA) / 2;
    *y0 = (int)(h - ny * AREA) / 2;
    return *nx * ny;
}

static bool in_circle(unsigned w, unsigned h, int tx, int ty)     /* the tile's far corner inside the circle */
{
    double r = 0.62 * w, dx = fabs(tx + (tx + AREA / 2 > (int)w / 2 ? AREA : 0) - w / 2.0),
           dy = fabs(ty + (ty + AREA / 2 > (int)h / 2 ? AREA : 0) - h / 2.0);
    return dx * dx + dy * dy <= r * r;
}

unsigned focus_ntiles(unsigned w, unsigned h)
{
    int x0, y0;
    unsigned nx;
    return grid(w, h, &x0, &y0, &nx);
}

double focus_tiles(const uint8_t *y, unsigned stride, unsigned w, unsigned h, unsigned from, unsigned n,
                   unsigned step, unsigned *count)
{
    int x0, y0;
    unsigned nx, all = grid(w, h, &x0, &y0, &nx);
    double s = 0;
    for (unsigned i = from, k = 0; k < n && i < all; k++, i += step) {
        int tx = x0 + (int)(i % nx) * AREA, ty = y0 + (int)(i / nx) * AREA;
        if (!in_circle(w, h, tx, ty))
            continue;
        s += area(y, stride, w, h, tx + AREA / 2, ty + AREA / 2);
        (*count)++;
    }
    return s;
}

double focus_score(const uint8_t *y, unsigned stride, unsigned w, unsigned h)
{
    unsigned n = 0;
    double s = focus_tiles(y, stride, w, h, 0, focus_ntiles(w, h), 1, &n);
    return n ? s / n * 100 : 0;
}

/* ---------------- the reference ---------------- */

static pthread_mutex_t fmu = PTHREAD_MUTEX_INITIALIZER;
static double ref[JOB_CAMS];
static int bad[JOB_CAMS], pct[JOB_CAMS] = { -1, -1 };
static bool loaded;

static void load(void)                  /* fmu held */
{
    if (loaded)
        return;
    loaded = true;
    FILE *f = fopen(FILE_PATH, "r");
    if (!f)
        return;
    if (fscanf(f, "%lf %lf %d %d %d %d", &ref[0], &ref[1], &bad[0], &bad[1], &pct[0], &pct[1]) != 6) {
        ref[0] = ref[1] = 0;
        bad[0] = bad[1] = 0;
        pct[0] = pct[1] = -1;
    }
    fclose(f);
}

static void save(void)                  /* fmu held */
{
    FILE *f = fopen(FILE_PATH, "w");
    if (!f) {
        logmsg("focus: %s not written", FILE_PATH);
        return;
    }
    fprintf(f, "%f %f %d %d %d %d\n", ref[0], ref[1], bad[0], bad[1], pct[0], pct[1]);
    fclose(f);
}

void focus_judge(int cam, double score)
{
    if (cam < 0 || cam >= JOB_CAMS)
        return;
    pthread_mutex_lock(&fmu);
    load();
    if (ref[cam] > 0) {
        long p = lround(100 * score / ref[cam]);
        pct[cam] = p > 999 ? 999 : (int)p;      /* DONE fits the BL616's 32-byte job field (metrics.c) */
        bad[cam] = pct[cam] < FOCUS_BAD_PCT ? bad[cam] + 1 : 0;
        save();                         /* the count across power-offs: on battery every check is a new boot */
    }
    logmsg("focus: cam%d score %.3f, reference %.3f: %d %%, %d low in a row", cam, score, ref[cam], pct[cam], bad[cam]);
    pthread_mutex_unlock(&fmu);
}

int focus_pct(int cam)
{
    pthread_mutex_lock(&fmu);
    load();
    int p = ref[cam] > 0 ? pct[cam] : -1;
    pthread_mutex_unlock(&fmu);
    return p;
}

bool focus_bad(int cam)
{
    pthread_mutex_lock(&fmu);
    load();
    bool b = ref[cam] > 0 && bad[cam] >= FOCUS_BAD_N;
    pthread_mutex_unlock(&fmu);
    return b;
}

/* ---------------- the session ---------------- */

#define VENC        0                   /* the H.265 channel (h265.c slot 0: VENC 1), the shown camera's */
#define TEXT        (1u << 30)          /* a record's kind bit: a text line (server/focus.go FocusText) */
#define CONNECT_S   20                  /* the bridge's DHCP may still run */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int want = -1;                   /* the camera shown, -1 before the first command */
static uint64_t want_ms;                /* when that command came */
static bool stopping;
static uint64_t until;                  /* the session's longest (the capture's secs) */
static unsigned seq;
static int shown = -1;                  /* the camera the encoder was fed last */
#define SCORE_PARTS 14                  /* frames per whole-frame score (70 tiles, 5 a frame): one a second at 15 fps
                                         * (user 2026-11-13: "feedback speed of 1 second to user is adequate", "score
                                         * on average whole frame") */
static unsigned part, part_n;           /* the shown camera's score in the making: frames, tiles scored so far */
static double part_sum;
static bool skipping;                   /* the queue was full: pictures dropped up to the next IDR */

struct record {
    struct record *next;
    bool pic;                           /* a picture (not text) */
    size_t len;                         /* data: [u32 length | kind][u64 us][payload] */
    uint8_t data[];
};
static pthread_mutex_t qmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcv = PTHREAD_COND_INITIALIZER;
static struct record *head, *tail;
static size_t queued;
static bool sending;
static unsigned sent, dropped;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}

static int shown_cam(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&mu);
    int c = want;
    pthread_mutex_unlock(&mu);
    return c;
}

static bool go(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&mu);
    bool g = !stopping && now_ms() < until;
    pthread_mutex_unlock(&mu);
    return g;
}

static void stop(const char *why)
{
    pthread_mutex_lock(&mu);
    if (!stopping)
        logmsg("focus: %s", why);
    stopping = true;
    pthread_mutex_unlock(&mu);
}

/* a record for the upload; a picture only when the queue has room (else dropped up to the next IDR) */
static void put(bool text, uint64_t us, const void *p, size_t n, bool idr)
{
    struct record *r = malloc(sizeof(*r) + 12 + n);
    pthread_mutex_lock(&qmu);
    if (!text) {
        if (!skipping && queued + n > QUEUE_MAX) {
            skipping = true;
            h265_idr(VENC);
        }
        if (skipping && idr && queued + n <= QUEUE_MAX)
            skipping = false;
    }
    if (r && (text || !skipping)) {
        uint32_t l = (uint32_t)n | (text ? TEXT : 0);
        r->next = NULL;
        r->pic = !text;
        r->len = 12 + n;
        for (int i = 0; i < 4; i++)
            r->data[i] = (uint8_t)(l >> (24 - 8 * i));
        for (int i = 0; i < 8; i++)
            r->data[4 + i] = (uint8_t)(us >> (56 - 8 * i));
        memcpy(r->data + 12, p, n);
        if (tail)
            tail->next = r;
        else
            head = r;
        tail = r;
        queued += r->len;
        r = NULL;
        pthread_cond_signal(&qcv);
    } else if (!text) {
        dropped++;
    }
    pthread_mutex_unlock(&qmu);
    free(r);
}

/* a switch: the other camera's pictures still waiting for the link go (stale: the new camera's IDR would wait
 * behind them); never before the session's first picture left (it carries the parameter sets). The bytes dropped.
 * Measured 2026-11-13 on a good link: nothing waiting at a switch (0 B), the K230 shows the camera 18-43 ms after
 * its command; the rest of a switch (0.2-0.6 s at the viewer) is the IDR's size on the link (209-848 KB) */
static size_t drop_pictures(void)
{
    size_t gone = 0;
    pthread_mutex_lock(&qmu);
    if (sent) {
        struct record **at = &head, *last = NULL;
        while (*at) {
            struct record *r = *at;
            if (r->pic) {
                *at = r->next;
                queued -= r->len;
                gone += r->len;
                dropped++;
                free(r);
            } else {
                last = r;
                at = &r->next;
            }
        }
        tail = last;
    }
    pthread_mutex_unlock(&qmu);
    return gone;
}

static void put_line(uint64_t us, const char *line)
{
    put(true, us, line, strlen(line), false);
}

/* the upload's writer: record after record (an IDR takes ~0.1 s on the link: the cameras' loop never waits for it) */
static void *sender(void *arg)
{
    int fd = *(int *)arg;
    for (;;) {
        pthread_mutex_lock(&qmu);
        while (!head && sending)
            pthread_cond_wait(&qcv, &qmu);
        struct record *r = head;
        if (r) {
            head = r->next;
            if (!head)
                tail = NULL;
        }
        pthread_mutex_unlock(&qmu);
        if (!r)
            break;
        bool ok = http_chunk(fd, r->data, r->len) == 0;
        pthread_mutex_lock(&qmu);
        queued -= r->len;
        if (ok && !(r->data[0] & 0x40))
            sent++;
        pthread_mutex_unlock(&qmu);
        free(r);
        if (!ok)
            stop("the upload failed");
    }
    return NULL;
}

static void frame(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride, const void *fr,
                  void *ctx)
{
    (void)uv;
    (void)ctx;
    int cam = (int)c->slot;
    uint64_t us = ((const k_video_frame_info *)fr)->v_frame.pts;
    if (cam != shown_cam(NULL))
        return;                         /* the other camera streams on, unshown: a switch is at the next frame */
    if (cam != shown) {                 /* the switch: this camera's first frame an IDR, CAM before it (the app
                                         * gives the pictures after it to this camera); its score from here */
        char l[16];
        size_t gone = shown >= 0 ? drop_pictures() : 0;
        pthread_mutex_lock(&mu);
        uint64_t asked = want_ms;
        pthread_mutex_unlock(&mu);
        logmsg("focus: cam%d shown %u ms after its command, %zu B of the other camera's pictures dropped", cam,
               (unsigned)(now_ms() - asked), gone);
        snprintf(l, sizeof(l), "CAM,%d", cam);
        put_line(us, l);
        h265_idr(VENC);
        shown = cam;
        part = 0;
        part_sum = 0;
        part_n = 0;
    }
    if (y) {                            /* the whole-frame score spread over a second: a few tiles per frame (the
                                         * cost of one frame's 5 areas), FOC when every tile was scored once */
        unsigned all = focus_ntiles(c->fw, c->fh), per = (all + SCORE_PARTS - 1) / SCORE_PARTS;
        part_sum += focus_tiles(y, stride, c->fw, c->fh, part * per, per, 1, &part_n);
        if (++part * per >= all) {
            char l[64];
            snprintf(l, sizeof(l), "FOC,%d,%u,%.3f", cam, seq, part_n ? part_sum / part_n * 100 : 0);
            put_line(us, l);
            part = 0;
            part_sum = 0;
            part_n = 0;
        }
    }
    uint8_t *au;
    int idr;
    size_t n = h265_frame(VENC, fr, &au, &idr);
    if (n) {
        put(false, us, au, n, idr);
        free(au);
    }
    seq++;
}

/* the reference kept: the person's Done (the best score FocusGuide saw); that camera's histories go (its picture
 * moved: the next check stores new ones, "new", not a leak) */
static void keep_reference(int cam, double score)
{
    pthread_mutex_lock(&fmu);
    load();
    ref[cam] = score;
    bad[cam] = 0;
    pct[cam] = 100;
    save();
    pthread_mutex_unlock(&fmu);
    job_forget(cam);
    logmsg("focus: cam%d reference %.3f, its histories deleted", cam, score);
}

struct where {
    const char *host, *port;
    char path[80];
};

/* the commands: the server's long poll, one answer after another until stop */
static void *poller(void *arg)
{
    const struct where *w = arg;
    while (go(NULL)) {
        char body[128];
        int st = http_get(w->host, w->port, w->path, body, sizeof(body), CONNECT_S);
        if (st != 200) {
            if (st == -1)               /* no connection: the server gone */
                stop("no answer from the server");
            continue;
        }
        const char *c = strstr(body, "\"cam\":"), *d = strstr(body, "\"done\":");
        if (strstr(body, "\"stop\":true"))
            stop("stopped (the app left, nobody watching, or the time is up)");
        if (c) {
            int cam = atoi(c + 6);
            if (cam >= 0 && cam < JOB_CAMS) {
                pthread_mutex_lock(&mu);
                want = cam;
                want_ms = now_ms();
                pthread_mutex_unlock(&mu);
            }
        }
        if (d) {
            int cam = shown_cam(NULL);
            double score = atof(d + 7);
            if (cam >= 0 && score > 0) {
                keep_reference(cam, score);
                char l[48];
                snprintf(l, sizeof(l), "FOCUS,OK,%d,%.3f", cam, score);
                put_line(0, l);
            }
        }
    }
    return NULL;
}

int focus_run(const char *host, const char *port, const char *dev, int secs, unsigned *frames)
{
    char path[80];
    snprintf(path, sizeof(path), "/v1/focus/%s/stream", dev);
    want = -1;
    stopping = false;
    until = now_ms() + (uint64_t)secs * 1000u;
    seq = 0;
    shown = -1;
    skipping = false;
    sent = dropped = 0;
    jpeg_hw_close();                    /* its VPU pages and pool for the H.265 channel (as media.c's videos) */
    if (h265_open(VENC, JOB_W, JOB_H, 15) != 0) {
        logmsg("focus: no H.265 channel");
        return -1;
    }
    int fd = http_open(host, port, path, "application/x-leakcam-focus", "", CONNECT_S);
    if (fd < 0) {
        h265_close(VENC);
        logmsg("focus: no connection to %s:%s", host, port);
        return -1;
    }
    struct where w = { host, port, "" };
    snprintf(w.path, sizeof(w.path), "/v1/focus/%s/cmd", dev);
    pthread_t ts, tp;
    sending = true;
    bool s_ok = pthread_create(&ts, NULL, sender, &fd) == 0;
    bool p_ok = s_ok && pthread_create(&tp, NULL, poller, &w) == 0;
    int rc = -1;
    if (p_ok) {
        uint64_t t0 = now_ms();         /* the first command brings the camera to show */
        while (shown_cam(NULL) < 0 && go(NULL) && now_ms() - t0 < 15000)
            usleep(20000);
        rc = shown_cam(NULL) < 0 ? -1 : job_focus(frame, NULL, go, shown_cam);
        stop("session over");
        pthread_join(tp, NULL);
    }
    if (rc != 0) {
        char l[24];
        snprintf(l, sizeof(l), "FOCUS,ERR,%d", shown_cam(NULL) < 0 ? 0 : shown_cam(NULL));
        put_line(0, l);
    }
    if (s_ok) {
        pthread_mutex_lock(&qmu);
        sending = false;
        pthread_cond_signal(&qcv);
        pthread_mutex_unlock(&qmu);
        pthread_join(ts, NULL);
    }
    int st = http_end(fd);
    h265_close(VENC);
    *frames = sent;
    logmsg("focus: session end, %u frames sent, %u dropped, the server said %d", sent, dropped, st);
    return rc == 0 && st == 200 ? 0 : -1;
}
