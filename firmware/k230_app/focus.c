/*
 * Camera focus (focus.h; DESIGN.md 5.3.17). The session: the phone's FOCUS,<cam> over the cable (the relay hands the
 * line here after the PIN) starts this thread, which holds the cameras (job_focus: the LEDs on, the exposure held) and
 * sends per frame
 *   FOC,<cam>,<seq>,<score>                   the sharpness (focus_score)
 * and once a second the centre crop as a JPEG (the VPU, jpeg_hw) in line-sized base64 parts
 *   FIMG,<cam>,<seq>,<part>,<parts>,<base64>
 * until FOCUS,STOP, another camera, or SESSION_MS (a phone gone without its STOP). FOCUS,<cam>,DONE,<score> keeps the
 * phone's reached score (FocusGuide's best, the median of 5) as the camera's reference -> FOCUS,OK,<cam>,<score>; that
 * camera's histories go (its picture moved: the next check stores new ones, "new", not a leak).
 */
#include "focus.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "k_video_comm.h"

#include "cap.h"
#include "job.h"
#include "jpeg_hw.h"
#include "leakcam.h"

#define FILE_PATH   "/sdcard/leakcam/focus"   /* "<ref0> <ref1> <bad0> <bad1> <pct0> <pct1>" */
#define AREA        256
#define SESSION_MS  (10 * 60 * 1000)
#define IMG_MS      1000                /* a preview a second: the score is what the person follows, the picture
                                         * only shows what the camera sees */
#define IMG_W       512                 /* the centre crop: native pixels, the detail the focus changes */
#define IMG_H       384
#define IMG_X       1024                /* the crop's corner: 64-byte aligned luma and chroma rows for the VPU */
#define IMG_Y       768
#define IMG_Q       40
#define B64_PART    120                 /* FIMG,<cam>,<seq>,<part>,<parts>, + 120 < the app's 160-byte line */

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

double focus_score(const uint8_t *y, unsigned stride, unsigned w, unsigned h)
{
    int cx = (int)w / 2, cy = (int)h / 2;
    int d = (int)(0.62 * w / 2 / M_SQRT2);    /* half the image circle's radius (imgdiff's 0.62 of the width) */
    double s = area(y, stride, w, h, cx, cy) + area(y, stride, w, h, cx - d, cy - d) +
               area(y, stride, w, h, cx + d, cy - d) + area(y, stride, w, h, cx - d, cy + d) +
               area(y, stride, w, h, cx + d, cy + d);
    return s / 5 * 100;
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

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int want = -1;                   /* the camera asked for, -1: none */
static uint64_t until;                  /* the session's end (a phone gone without its STOP) */
static bool running;
static unsigned seq;
static uint64_t img_at;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}

static bool go(void *ctx)
{
    int cam = *(int *)ctx;
    pthread_mutex_lock(&mu);
    bool g = want == cam && now_ms() < until;
    pthread_mutex_unlock(&mu);
    return g;
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* the centre crop of the frame as a JPEG, sent in FIMG parts */
static void preview(int cam, const void *frame)
{
    k_video_frame_info f = *(const k_video_frame_info *)frame;
    unsigned stride = f.v_frame.stride[0] ? f.v_frame.stride[0] : f.v_frame.width;
    k_u64 uv = f.v_frame.phys_addr[1] ? f.v_frame.phys_addr[1] : f.v_frame.phys_addr[0] + (k_u64)stride * f.v_frame.height;
    if (f.v_frame.width < IMG_X + IMG_W || f.v_frame.height < IMG_Y + IMG_H)
        return;
    f.v_frame.phys_addr[0] += (k_u64)IMG_Y * stride + IMG_X;   /* the crop: the same planes, another start, the */
    f.v_frame.phys_addr[1] = uv + (k_u64)(IMG_Y / 2) * stride + IMG_X;   /* frame's stride */
    f.v_frame.width = IMG_W;
    f.v_frame.height = IMG_H;
    uint8_t *jpg;
    size_t n = jpeg_hw(&f, IMG_W, IMG_H, IMG_Q, 0, &jpg);
    if (!n)
        return;
    size_t len = (n + 2) / 3 * 4;
    char *t = malloc(len + 1);
    if (t) {
        size_t k = 0;
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)jpg[i] << 16 | (i + 1 < n ? (uint32_t)jpg[i + 1] << 8 : 0) | (i + 2 < n ? jpg[i + 2] : 0);
            t[k++] = b64[v >> 18 & 63];
            t[k++] = b64[v >> 12 & 63];
            t[k++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
            t[k++] = i + 2 < n ? b64[v & 63] : '=';
        }
        unsigned parts = (unsigned)((len + B64_PART - 1) / B64_PART);
        char l[160];
        for (unsigned p = 0; p < parts; p++) {
            size_t at = (size_t)p * B64_PART, m = len - at < B64_PART ? len - at : B64_PART;
            snprintf(l, sizeof(l), "FIMG,%d,%u,%u,%u,%.*s", cam, seq, p, parts, (int)m, t + at);
            if (!usb_line(l))
                break;                  /* no phone reading: the rest would only wait */
        }
        free(t);
    }
    free(jpg);
}

static void frame(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride, const void *fr,
                  void *ctx)
{
    (void)uv;
    int cam = *(int *)ctx;
    if (!y)
        return;
    char l[64];
    snprintf(l, sizeof(l), "FOC,%d,%u,%.3f", cam, seq, focus_score(y, stride, c->fw, c->fh));
    usb_line(l);
    uint64_t t = now_ms();
    if (t - img_at >= IMG_MS) {
        img_at = t;
        preview(cam, fr);
    }
    seq++;
}

static void *session(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (want < 0)
            pthread_cond_wait(&cv, &mu);
        int cam = want;
        pthread_mutex_unlock(&mu);
        seq = 0;
        img_at = 0;
        step("focus: session");
        int rc = job_focus(cam, frame, &cam, go);
        pthread_mutex_lock(&mu);
        if (want == cam)                /* not switched: stopped by the time limit, or the camera failed */
            want = -1;
        pthread_mutex_unlock(&mu);
        if (rc != 0) {
            char l[24];
            snprintf(l, sizeof(l), "FOCUS,ERR,%d", cam);
            usb_line(l);
        }
        step("focus: session end");
    }
    return NULL;
}

const char *focus_line(const char *line)
{
    static char answer[48];
    int cam;
    double score;
    char tail;
    if (!strcmp(line, "FOCUS,STOP")) {
        pthread_mutex_lock(&mu);
        want = -1;
        pthread_mutex_unlock(&mu);
        return NULL;
    }
    if (sscanf(line, "FOCUS,%d,DONE,%lf%c", &cam, &score, &tail) == 2 && cam >= 0 && cam < JOB_CAMS && score > 0) {
        pthread_mutex_lock(&fmu);
        load();
        ref[cam] = score;
        bad[cam] = 0;
        pct[cam] = 100;
        save();
        pthread_mutex_unlock(&fmu);
        job_forget(cam);
        logmsg("focus: cam%d reference %.3f, its histories deleted", cam, score);
        snprintf(answer, sizeof(answer), "FOCUS,OK,%d,%.3f", cam, score);
        return answer;
    }
    if (sscanf(line, "FOCUS,%d%c", &cam, &tail) == 1 && cam >= 0 && cam < JOB_CAMS) {
        pthread_mutex_lock(&mu);
        want = cam;
        until = now_ms() + SESSION_MS;
        if (!running) {
            pthread_t t;
            running = pthread_create(&t, NULL, session, NULL) == 0;
            if (!running)
                logmsg("focus: thread not started");
        }
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&mu);
        return NULL;
    }
    return "ERR CMD";
}
