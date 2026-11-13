#include "media.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "focus.h"
#include "h265.h"
#include "http_post.h"
#include "jpeg_hw.h"
#include "k_video_comm.h"
#include "job.h"
#include "leakcam.h"

#define QUALITY       85
#define MAX_PHOTOS    8                 /* the server's leak: 4 (2 per camera) */
#define WIFI_WAIT_MS  5000              /* WIFI's answer through the relay (5 sends of 300 ms) */
#define RESCAN_S      10                /* the BL616's SDIO device is up within ~1 s of the WIFI's ACK (capture.c) */
#define CONNECT_S     20                /* the first upload: the BL616's join + the K230's DHCP (test100: <1 s each) */
#define TRIES         3                 /* an upload again after a failure: a check-in takes the radio a few s */
#define BL616_IOCTL_RESCAN 0x4c01       /* bl616_nethub.h: /dev/bl616 binds the SDIO card */
#define VIDEO_FPS     15                /* the sensors' native mode (vicap_cap.c) */
#define VIDEO_TYPE    "video/x-leakcam-h265"   /* the server's framing (server/video.go): [u32 BE length]
                                                 * [u64 BE the frame's time, us][AU] ...: the real rate, any drop */
#define VIDEO_QUEUE   (2560u << 10)     /* encoded, not yet sent, per camera: past it frames are dropped up to the next
                                         * IDR (vencode). ~2 s at 10 Mbit/s; normal backlog peaks 1.2-1.9 MB
                                         * (tests/test148). It comes from the page pool beside the two H.265 channels
                                         * (k230d_rtos_leakcam_defconfig): 12 MB a camera ran the pool out first
                                         * ("Memory exhaustion!", both videos lost, 2026-11-13) */

struct req {
    char kind[8], base[40], host[48], port[8];
    int cams, photos, gap_ms, secs;
};

struct photo {
    uint8_t *jpeg;
    size_t len;
    int n;                              /* p<n> */
    long long taken_ms;                 /* unix ms, 0: no clock */
};

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static struct req queue[2];
static unsigned q_n;
static bool wifi_want, wifi_done, wifi_ok, result_ready;
static char result[32];

static long long ms_since(const struct timespec *t0)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0->tv_sec) * 1000LL + (t.tv_nsec - t0->tv_nsec) / 1000000;
}

/* WIFI through the relay, then the SDIO card bound (wlan0 then comes up and takes its address from DHCP) */
static bool bridge_up(void)
{
    pthread_mutex_lock(&mu);
    wifi_want = true;
    wifi_done = false;
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += WIFI_WAIT_MS / 1000;
    while (!wifi_done && pthread_cond_timedwait(&cv, &mu, &end) == 0)
        ;
    bool ok = wifi_done && wifi_ok;
    wifi_want = false;
    pthread_mutex_unlock(&mu);
    if (!ok) {
        logmsg("media: WIFI not taken by the BL616");
        return false;
    }
    /* by time, not tries: right after the K230's boot /dev/bl616 may not exist yet (ENOENT 1 s after the start: every
     * try failed at once, test143 --cold 2026-11-12); a rescan itself waits up to 3 s for the card */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (ms_since(&t0) < RESCAN_S * 1000) {
        int fd = open("/dev/bl616", O_RDWR);
        bool up = fd >= 0 && ioctl(fd, BL616_IOCTL_RESCAN, NULL) == 0;
        if (fd >= 0)
            close(fd);
        if (up)
            return true;
        usleep(250000);
    }
    logmsg("media: the BL616's SDIO card did not come up: %s", strerror(errno));
    return false;
}

struct shot {                           /* one request's pictures, filled by encode() per camera and round */
    const struct req *r;
    struct photo *ph;
    int n, failed, round;
    struct timespec taken;              /* the round's moment (job_shoot) */
};

/* a camera's frame, mapped in its video buffer (job_shoot -> cap_release_all): a JPEG of it, the moment in EXIF */
static void encode(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride, const void *frame,
                   void *ctx)
{
    struct shot *sh = ctx;
    int id = sh->round * sh->r->cams + (int)c->slot + 1;
    if ((int)c->slot >= sh->r->cams || id > sh->r->photos || sh->n >= MAX_PHOTOS)
        return;                                  /* a camera this request does not ask for */
    /* the moment: the job's, after the shot; no clock (the BL616 had none to give): no time in the picture, the
     * server keeps the upload's */
    struct timespec now;
    job_shot_time(&now);                         /* the shot's moment, the same for every camera of the round */
    time_t when = now.tv_sec > 1767225600 ? now.tv_sec : 0;   /* 2026-01-01 */
    struct photo *p = &sh->ph[sh->n];
    (void)y;
    (void)uv;
    (void)stride;
    p->len = jpeg_hw(frame, c->width, c->height, QUALITY, when, &p->jpeg);   /* the video encoder, no CPU */
    p->n = id;
    p->taken_ms = when ? (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 : 0;
    if (!p->len) {
        logmsg("media: p%d: JPEG failed", id);
        sh->failed++;
        return;
    }
    sh->n++;
}

/* ---- videos: per camera the H.265 channel and a chunked upload, the frames sent as they are encoded (the server
 * serves them live and writes the MP4 at the end, server/video.go) ---- */

struct vframe {
    struct vframe *next;
    size_t len;                         /* data: [u32 BE length][u64 BE pts us][the access unit]; the length's top
                                         * bit set: an Opus packet (audio.c), the same clock */
    uint8_t data[];
};

struct vcam {
    pthread_t t;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    struct vframe *head, *tail;
    size_t queued, bytes, queued_max;
    int fd, frames, sent, idrs, status, dropped, drops;   /* dropped frames, in how many gaps */
    int audio, audio_lost;              /* Opus packets queued, lost (the queue full) */
    long long enc_us, wait_us, send_us;   /* time in the encoder, waiting for the camera, in send() */
    struct timespec last;
    bool end, failed, running, skipping;   /* skipping: the backlog was full, frames dropped up to the next IDR */
};

/* a camera's uploader: frame after frame to its chunked POST until the end, then the server's answer */
static void *vsend(void *arg)
{
    struct vcam *v = arg;
    for (;;) {
        pthread_mutex_lock(&v->mu);
        while (!v->head && !v->end)
            pthread_cond_wait(&v->cv, &v->mu);
        struct vframe *f = v->head;
        if (f) {
            v->head = f->next;
            if (!v->head)
                v->tail = NULL;
        }
        pthread_mutex_unlock(&v->mu);
        if (!f)
            break;                      /* the end, all sent */
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        bool ok = !v->failed && http_chunk(v->fd, f->data, f->len) == 0;
        clock_gettime(CLOCK_MONOTONIC, &b);
        v->send_us += (b.tv_sec - a.tv_sec) * 1000000LL + (b.tv_nsec - a.tv_nsec) / 1000;
        pthread_mutex_lock(&v->mu);
        v->queued -= f->len;
        if (!ok)
            v->failed = true;
        else if (!(f->data[0] & 0x80))  /* a picture, not an Opus packet */
            v->sent++;
        pthread_mutex_unlock(&v->mu);
        free(f);
    }
    if (v->failed) {                    /* a chunk may be half sent: no end marker after it, the server sees the cut */
        close(v->fd);
        v->status = -2;
    } else {
        v->status = http_end(v->fd);
    }
    return NULL;
}

static struct vcam vc[JOB_CAMS];

/* a camera's frame (job_video -> cap_release_all / cap_stream): encoded, queued for its uploader */
static void vencode(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride, const void *frame,
                    void *ctx)
{
    (void)y;
    (void)uv;
    (void)stride;
    (void)ctx;
    struct vcam *v = &vc[c->slot];
    if (c->slot >= JOB_CAMS || !v->running || c->fw != JOB_W || c->fh != JOB_H)
        return;
    uint8_t *au;
    int idr;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    if (v->last.tv_sec)
        v->wait_us += (a.tv_sec - v->last.tv_sec) * 1000000LL + (a.tv_nsec - v->last.tv_nsec) / 1000;
    size_t n = h265_frame((int)c->slot, frame, &au, &idr);
    clock_gettime(CLOCK_MONOTONIC, &b);
    v->enc_us += (b.tv_sec - a.tv_sec) * 1000000LL + (b.tv_nsec - a.tv_nsec) / 1000;
    v->last = b;
    if (!n)
        return;
    struct vframe *f = malloc(sizeof(*f) + 12 + n);
    if (f) {
        uint64_t pts = ((const k_video_frame_info *)frame)->v_frame.pts;   /* the camera's own clock, us */
        if (!v->frames)                 /* against the microphone's (audio.c "first packet"): one clock for both */
            logmsg("media: v%d: first picture time %llu us, monotonic %lld us", c->slot + 1,
                   (unsigned long long)pts, (long long)b.tv_sec * 1000000LL + b.tv_nsec / 1000);
        f->next = NULL;
        f->len = 12 + n;
        for (int i = 0; i < 4; i++)
            f->data[i] = (uint8_t)(n >> (24 - 8 * i));
        for (int i = 0; i < 8; i++)
            f->data[4 + i] = (uint8_t)(pts >> (56 - 8 * i));
        memcpy(f->data + 12, au, n);
    }
    free(au);
    pthread_mutex_lock(&v->mu);
    /* the link slower than the encoder for a while (the backlog at VIDEO_QUEUE: two cameras' 20 Mbit/s against a Wi-Fi
     * dip, test148 2026-11-13, a leak's videos): its frames dropped up to the next IDR (an IDR every second, h265.c),
     * which the queue has room for - a gap in the video, not the video lost. Each frame carries its camera time, the
     * server times the samples by it (server/video.go) */
    if (f && v->queued + f->len > VIDEO_QUEUE && !v->skipping) {
        v->skipping = true;
        v->drops++;
    }
    if (f && v->skipping && idr)
        v->skipping = v->queued + f->len > VIDEO_QUEUE;
    if (f && v->skipping) {
        v->dropped++;
        pthread_mutex_unlock(&v->mu);
        free(f);
        return;
    }
    if (f && !v->failed) {
        if (v->tail)
            v->tail->next = f;
        else
            v->head = f;
        v->tail = f;
        v->queued += f->len;
        if (v->queued > v->queued_max)
            v->queued_max = v->queued;
        v->bytes += n;
        v->frames++;
        v->idrs += idr;
        f = NULL;
        pthread_cond_signal(&v->cv);
    } else {
        v->failed = true;               /* no memory for the frame: this camera's video is lost */
    }
    pthread_mutex_unlock(&v->mu);
    free(f);
}

/* the microphone's packets (audio.c, its thread) into every camera's upload: each video carries the sound */
static void vaudio(const uint8_t *pkt, size_t n, uint64_t us, void *ctx)
{
    (void)ctx;
    for (int c = 0; c < JOB_CAMS; c++) {
        struct vcam *v = &vc[c];
        if (!v->running)
            continue;
        struct vframe *f = malloc(sizeof(*f) + 12 + n);
        pthread_mutex_lock(&v->mu);
        if (f && !v->failed && !v->end && v->frames && v->queued + 12 + n <= VIDEO_QUEUE) {   /* from the first picture */
            f->next = NULL;
            f->len = 12 + n;
            uint32_t len = (uint32_t)n | 0x80000000u;
            for (int i = 0; i < 4; i++)
                f->data[i] = (uint8_t)(len >> (24 - 8 * i));
            for (int i = 0; i < 8; i++)
                f->data[4 + i] = (uint8_t)(us >> (56 - 8 * i));
            memcpy(f->data + 12, pkt, n);
            if (v->tail)
                v->tail->next = f;
            else
                v->head = f;
            v->tail = f;
            v->queued += f->len;
            v->audio++;
            f = NULL;
            pthread_cond_signal(&v->cv);
        } else if (v->frames && !v->end) {
            v->audio_lost++;
        }
        pthread_mutex_unlock(&v->mu);
        free(f);
    }
}

/* the request's videos (one per camera, secs long): the counts of uploaded and failed */
static void videos(const struct req *r, bool net, const struct timespec *t0, int *sent, int *failed)
{
    char what[320];
    int cams = r->cams < JOB_CAMS ? r->cams : JOB_CAMS;
    unsigned frames = (unsigned)r->secs * VIDEO_FPS;
    jpeg_hw_close();                    /* its VPU pages and pool for the two H.265 channels (14 MB of pages each) */
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long long taken = now.tv_sec > 1767225600 ? (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 : 0;
    for (int c = 0; c < cams; c++) {
        struct vcam *v = &vc[c];
        memset(v, 0, sizeof(*v));
        pthread_mutex_init(&v->mu, NULL);
        pthread_cond_init(&v->cv, NULL);
        char path[80], hdr[96];
        snprintf(path, sizeof(path), "/v1/media/%s-v%d", r->base, c + 1);
        if (taken)                      /* the clip's moment: the server's MP4 creation time and X-Taken */
            snprintf(hdr, sizeof(hdr), "X-Fps: %d\r\nX-Audio: opus/%d/1\r\nX-Taken: %lld", VIDEO_FPS, AUDIO_RATE, taken);
        else
            snprintf(hdr, sizeof(hdr), "X-Fps: %d\r\nX-Audio: opus/%d/1", VIDEO_FPS, AUDIO_RATE);
        v->fd = net ? http_open(r->host, r->port, path, VIDEO_TYPE, hdr, CONNECT_S) : -1;
        if (v->fd < 0 || h265_open(c, JOB_W, JOB_H, VIDEO_FPS) != 0) {
            logmsg("media: v%d: %s", c + 1, v->fd < 0 ? "no connection" : "no H.265 channel");
            if (v->fd >= 0)
                http_end(v->fd);
            continue;
        }
        v->running = pthread_create(&v->t, NULL, vsend, v) == 0;
        if (!v->running) {
            http_end(v->fd);
            h265_close(c);
        }
    }
    snprintf(what, sizeof(what), "media: video %u frames per camera at %d fps, %d kbit/s", frames, VIDEO_FPS,
             H265_KBPS);
    step(what);
    struct timespec taken_at;
    long long ts = ms_since(t0);
    audio_sink(vaudio, NULL);           /* the sound from each video's first picture (vaudio) */
    int mask = frames ? job_video(frames - 1, (unsigned)r->secs * 1000, vencode, NULL, &taken_at) : 0;
    audio_sink(NULL, NULL);
    long long te = ms_since(t0);
    for (int c = 0; c < cams; c++) {
        struct vcam *v = &vc[c];
        if (!v->running) {
            (*failed)++;
            continue;
        }
        pthread_mutex_lock(&v->mu);
        v->end = true;
        pthread_cond_signal(&v->cv);
        pthread_mutex_unlock(&v->mu);
        pthread_join(v->t, NULL);
        h265_close(c);
        bool ok = !v->failed && v->status == 200 && (mask & 1 << c) && v->frames > 0;
        snprintf(what, sizeof(what), "media: v%d: %d frames (%d IDR), %d dropped in %d gaps, audio %d packets (%d lost), in %lld ms, %zu B = "
                 "%lld kbit/s, %d sent, %s %d "
                 "at %lld ms; encode %lld ms, between frames %lld ms, send %lld ms, backlog max %zu B", c + 1,
                 v->frames, v->idrs, v->dropped, v->drops, v->audio, v->audio_lost, te - ts, v->bytes, te > ts ? (long long)v->bytes * 8 / (te - ts) : 0, v->sent,
                 ok ? "uploaded" : "upload failed", v->status, ms_since(t0), v->enc_us / 1000, v->wait_us / 1000,
                 v->send_us / 1000, v->queued_max);
        step(what);
        if (ok)
            (*sent)++;
        else
            (*failed)++;
        pthread_mutex_destroy(&v->mu);
        pthread_cond_destroy(&v->cv);
    }
}

static void run(const struct req *r)
{
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    char what[160];
    snprintf(what, sizeof(what), "media: %s %s, %d pictures, %d cameras", r->kind, r->base, r->photos, r->cams);
    step(what);
    bool video = r->secs > 0 && (!strcmp(r->kind, "video") || !strcmp(r->kind, "leak"));
    bool focus = r->secs > 0 && !strcmp(r->kind, "focus");
    bool net = (r->photos > 0 || video || focus) && bridge_up();   /* first: the join and DHCP run while the pictures
                                                                    * are taken */
    snprintf(what, sizeof(what), "media: Wi-Fi bridge %s", net ? "bound" : "not up");
    step(what);
    if (focus) {                        /* the app's focus adjustment (focus.c, DESIGN.md 5.3.17): its session, then SENT */
        char dev[16];
        snprintf(dev, sizeof(dev), "%.*s", (int)strcspn(r->base, "-"), r->base);
        unsigned frames = 0;
        int rc = net ? focus_run(r->host, r->port, dev, r->secs, &frames) : -1;
        pthread_mutex_lock(&mu);
        snprintf(result, sizeof(result), "%d,%d,0", rc == 0, rc != 0);
        result_ready = true;
        pthread_mutex_unlock(&mu);
        snprintf(what, sizeof(what), "media: focus session %s, %u frames", rc == 0 ? "done" : "failed", frames);
        step(what);
        return;
    }

    static struct photo ph[MAX_PHOTOS];
    struct shot sh = { .r = r, .ph = ph, .n = 0, .failed = 0 };
    int rounds = r->cams > 0 ? (r->photos + r->cams - 1) / r->cams : 0;
    for (int round = 0; round < rounds; round++) {
        if (round) {                                 /* the next round gap_ms after the first */
            long long wait = (long long)round * r->gap_ms - ms_since(&t0);
            if (wait > 0)
                usleep((useconds_t)(wait * 1000));
        }
        long long tc = ms_since(&t0);
        sh.round = round;
        int n0 = sh.n;
        int mask = job_shoot(encode, &sh, &sh.taken);   /* encode() per camera, the frame read in place */
        for (int c = 0; c < r->cams && c < JOB_CAMS; c++) {
            int id = round * r->cams + c + 1;
            if (id <= r->photos && !(mask & 1 << c)) {
                logmsg("media: p%d: camera %d gave no frame", id, c);
                sh.failed++;
            }
        }
        snprintf(what, sizeof(what), "media: round %d at %lld ms: cameras 0x%x, %d JPEGs (%zu + %zu B) at %lld ms",
                 round + 1, tc, mask, sh.n - n0, sh.n > n0 ? ph[n0].len : 0, sh.n > n0 + 1 ? ph[n0 + 1].len : 0,
                 ms_since(&t0));
        step(what);
    }
    int n = sh.n, failed = sh.failed;

    int sent = 0;
    for (int i = 0; i < n; i++) {
        char path[80], hdr[48];
        snprintf(path, sizeof(path), "/v1/media/%s-p%d", r->base, ph[i].n);
        if (ph[i].taken_ms)
            snprintf(hdr, sizeof(hdr), "X-Taken: %lld", ph[i].taken_ms);
        else
            hdr[0] = 0;
        int st = -1;
        for (int t = 0; net && t < TRIES && st != 200; t++) {
            if (t)
                sleep(1);
            st = http_post(r->host, r->port, path, "image/jpeg", hdr, ph[i].jpeg, ph[i].len, sent ? 5 : CONNECT_S);
        }
        snprintf(what, sizeof(what), "media: p%d %zu B: %s %d at %lld ms", ph[i].n, ph[i].len,
                 st == 200 ? "uploaded" : "upload failed", st, ms_since(&t0));
        step(what);
        if (st == 200)
            sent++;
        else
            failed++;
        free(ph[i].jpeg);
    }
    if (video)                                       /* after the pictures: one per camera, secs long */
        videos(r, net, &t0, &sent, &failed);
    int skipped = 0;
    pthread_mutex_lock(&mu);
    snprintf(result, sizeof(result), "%d,%d,%d", sent, failed, skipped);
    result_ready = true;
    pthread_mutex_unlock(&mu);
    snprintf(what, sizeof(what), "media: done, %d uploaded, %d failed, %d skipped", sent, failed, skipped);
    step(what);
}

static void *media_thread(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!q_n)
            pthread_cond_wait(&cv, &mu);
        struct req r = queue[0];
        queue[0] = queue[1];
        q_n--;
        pthread_mutex_unlock(&mu);
        run(&r);
    }
    return NULL;
}

void media_start(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, media_thread, NULL) != 0)
        logmsg("media: thread not started");
}

void media_request(const char *arg)
{
    struct req r;
    memset(&r, 0, sizeof(r));
    if (sscanf(arg, "%7[^,],%39[^,],%d,%d,%d,%d,%47[^,],%7s", r.kind, r.base, &r.cams, &r.photos, &r.gap_ms, &r.secs,
               r.host, r.port) != 8 || r.cams < 1 || r.cams > JOB_CAMS || r.photos < 0 || r.photos > MAX_PHOTOS ||
        r.gap_ms < 0 || r.gap_ms > 60000) {
        logmsg("media: SHOOT not understood: %s", arg);
        pthread_mutex_lock(&mu);
        snprintf(result, sizeof(result), "0,0,0");   /* answered all the same: the BL616 waits for SENT */
        result_ready = true;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_lock(&mu);
    if (q_n < 2)
        queue[q_n++] = r;
    else
        logmsg("media: SHOOT dropped: two waiting");
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

bool media_wifi_wanted(void)
{
    pthread_mutex_lock(&mu);
    bool w = wifi_want && !wifi_done;
    pthread_mutex_unlock(&mu);
    return w;
}

void media_wifi_answer(bool acked)
{
    pthread_mutex_lock(&mu);
    wifi_ok = acked;
    wifi_done = true;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

bool media_result(char *buf, size_t len)
{
    pthread_mutex_lock(&mu);
    bool got = result_ready;
    if (got)
        snprintf(buf, len, "%s", result);
    result_ready = false;
    pthread_mutex_unlock(&mu);
    return got;
}
