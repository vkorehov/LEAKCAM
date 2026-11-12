/*
 * The leak detection job (DESIGN.md 5.3.8, 5.3.12) on the two real cameras (vicap_cap.c; until 2026-11-12 the cameras
 * were simulated, user 2026-11-07).
 *
 * A check: per camera a 320x240 luma frame, compared with that camera's history (imgdiff: blocks, gain-normalised, so
 * a change of the light level alone reads as the same scene):
 *   new      no history yet: the frame becomes the history
 *   same     nothing changed
 *   changed  a change imgdiff sees; the history is kept (it changes only once the server has seen the change and said
 *            "no leak", a later step)
 *   light    (the change net's answer for a change of the light only: not built yet)
 * The history is /sdcard/leakcam/cam<N>.hist, the raw frame.
 *
 * Capture: both cameras at once (offline VICAP, 1280x960 binned, SETTLE frames for AE/AWB first, ~1.6 s for the pair,
 * tests/test030), each luma frame averaged 4x4 down to 320x240. A camera that gives no frame reads "nocam".
 *
 * The test override (user 2026-11-12: "update the stored one through a special command"; the new image still comes
 * from the camera and so differs): /sdcard/leakcam/sim, the last `cam<N>=<token>` per camera counts, so the K230
 * console's `echo` (which appends and cannot create a file) sets it, `rm` ends it (the job creates it empty again).
 * The tokens change only the stored image the real frame is compared with, in memory (the file stays the camera's):
 *   object  a puddle painted into it (brightness inverted there): the real frame reads "changed"
 *   light   it 40 % darker: imgdiff's gain normalisation must still read "same"
 *   store   the real frame replaces the stored image (a new baseline, written to the file)
 * Without a token every check is a plain real compare.
 */
#include "job.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cap.h"
#include "imgdiff.h"
#include "leakcam.h"
#include "led.h"

#define CAMS     2
#define DIR      "/sdcard/leakcam"
#define SIM_FILE DIR "/sim"
#define FRAME    (IMGDIFF_W * IMGDIFF_H)

#define CAP_W    1280                   /* the OV5647 binned mode (vicap_cap.c) */
#define CAP_H    960
#define SETTLE   20                     /* frames dropped for AE/AWB (cam_shot's default, tests/test030) */

/* the LEDs on for the shot (DESIGN.md: "LEDs on for the shot, AE settled"; white and IR at full, as leakcam_wake): a
 * dark room gave all-zero frames and every check read "same" (2026-11-12 05:00, the room lights off) */
static unsigned led_percent[2] = { 100, 100 };

static int grab_lit(struct cap_cam *cams, int n)
{
    if (led_set(true, led_percent, 0) < 0)
        logmsg("job: LEDs not on: the shot without full illumination");
    int rc = cap_grab_all(cams, n, SETTLE, 1000);
    led_set(false, led_percent, 0);
    return rc;
}

enum scene { SCENE_SAME, SCENE_LIGHT, SCENE_OBJECT, SCENE_STORE };

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static unsigned pending = 1;            /* the check at the start */
static char result[32];
static bool result_ready;
static uint8_t raw[CAMS][FRAME], cur[FRAME], hist[FRAME];
static bool got[CAMS];                  /* this check's frame of camera N arrived */

static enum scene sim_scene(int cam)
{
    char buf[256] = { 0 }, key[8];
    int fd = open(SIM_FILE, O_RDONLY);
    if (fd < 0) {
        fd = open(SIM_FILE, O_WRONLY | O_CREAT, 0644);   /* empty: same; echo appends to it */
        if (fd >= 0)
            close(fd);
        return SCENE_SAME;
    }
    off_t end = lseek(fd, 0, SEEK_END);
    if (end > (off_t)sizeof(buf) - 1)
        lseek(fd, end - (off_t)sizeof(buf) + 1, SEEK_SET);   /* the newest tokens */
    else
        lseek(fd, 0, SEEK_SET);
    (void)!read(fd, buf, sizeof(buf) - 1);
    close(fd);
    snprintf(key, sizeof(key), "cam%d=", cam);
    const char *p = NULL;
    for (const char *q = buf; (q = strstr(q, key)) != NULL; q++)
        p = q;                                            /* the last one */
    if (!p)
        return SCENE_SAME;
    p += strlen(key);
    return !strncmp(p, "light", 5) ? SCENE_LIGHT : !strncmp(p, "object", 6) ? SCENE_OBJECT :
           !strncmp(p, "store", 5) ? SCENE_STORE : SCENE_SAME;
}

/* the test override on the stored image (the file header above): the puddle or the darker room, in memory */
static void sim_apply(enum scene sc, uint8_t *img)
{
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            uint8_t *v = &img[y * IMGDIFF_W + x];
            if (sc == SCENE_OBJECT) {
                int dx = x - 190, dy = y - 180;
                if (dx * dx * 9 + dy * dy * 49 < 40 * 40 * 9)
                    *v = *v < 128 ? 230 : 22;   /* a puddle that always differs from the real frame: bright on a
                                                 * dark floor, dark on a light one (a fixed 22 matched a dark room) */
            } else if (sc == SCENE_LIGHT) {
                *v = (uint8_t)(*v * 6 / 10);
            }
        }
}

/* a camera's luma frame 4x4 averaged to the compare's 320x240 */
static void reduce4(const struct cap_cam *c, uint8_t *out)
{
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            unsigned sum = 0;
            for (int dy = 0; dy < 4; dy++)
                for (int dx = 0; dx < 4; dx++)
                    sum += c->luma[(size_t)(y * 4 + dy) * c->width + (size_t)x * 4 + dx];
            out[y * IMGDIFF_W + x] = (uint8_t)(sum / 16);
        }
}

/* The cameras stay open for the program's life: VICAP init/deinit per check left threads and kernel heap behind
 * (2026-11-12: 36 threads and 1.87 of ~7.3 MB kernel heap left after ~20 checks, then the job hung; cam_shot never
 * showed it, its process ends after one picture). A check only starts and stops the stream; a failed grab closes them
 * and the next check opens them again. */
static struct cap_cam cams[CAMS];
static int ncams = -1;                  /* open: 0..CAMS (the cameras that answered); -1: to open */

static int open_cams(void)
{
    memset(cams, 0, sizeof(cams));
    for (int i = 0; i < CAMS; i++)
        cams[i].node = (unsigned)i;
    if (cap_open_all(cams, CAMS, CAP_W, CAP_H) == 0)
        return CAMS;
    cap_close_all(cams, CAMS);
    /* one camera missing or broken stops the shared open: find the ones that answer, then open those together */
    unsigned ok[CAMS];
    int n = 0;
    for (int i = 0; i < CAMS; i++) {
        struct cap_cam one;
        memset(&one, 0, sizeof(one));
        one.node = (unsigned)i;
        if (cap_open_all(&one, 1, CAP_W, CAP_H) == 0)
            ok[n++] = (unsigned)i;
        cap_close_all(&one, 1);
    }
    memset(cams, 0, sizeof(cams));
    for (int i = 0; i < n; i++)
        cams[i].node = ok[i];
    if (n && cap_open_all(cams, n, CAP_W, CAP_H) != 0) {
        cap_close_all(cams, n);
        return 0;
    }
    logmsg("job: %d of %d cameras answer", n, CAMS);
    return n;
}

/* one frame of each open camera into raw[] (got[] says which), both streaming at once */
static void capture(void)
{
    for (int i = 0; i < CAMS; i++)
        got[i] = false;
    if (ncams < 0)
        ncams = open_cams();
    if (ncams <= 0)
        return;
    if (grab_lit(cams, ncams) != 0) {
        cap_close_all(cams, ncams);
        ncams = -1;                     /* opened again at the next check */
        return;
    }
    for (int i = 0; i < ncams; i++) {
        reduce4(&cams[i], raw[cams[i].slot]);
        got[cams[i].slot] = true;
    }
}

static bool hist_path(int cam, char *p, size_t n)
{
    return snprintf(p, n, DIR "/cam%d.hist", cam) < (int)n;
}

static bool load(const char *path, uint8_t *buf)
{
    int fd = open(path, O_RDONLY);
    bool ok = fd >= 0 && read(fd, buf, FRAME) == FRAME;
    if (fd >= 0)
        close(fd);
    return ok;
}

static bool save(const char *path, const uint8_t *buf)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = fd >= 0 && write(fd, buf, FRAME) == FRAME;
    if (fd >= 0)
        close(fd);
    return ok;
}

static const char *check_cam(int cam)
{
    char path[48], what[112];
    if (!got[cam]) {
        snprintf(what, sizeof(what), "job: cam%d nocam: no frame", cam);
        step(what);
        return "nocam";
    }
    enum scene sc = sim_scene(cam);
    imgdiff_reduce(raw[cam], IMGDIFF_W, IMGDIFF_H, IMGDIFF_W, cur);    /* the 3x3 smoothing */
    hist_path(cam, path, sizeof(path));
    const char *state;
    if (!load(path, hist) || sc == SCENE_STORE) {
        state = "new";
        if (!save(path, cur))
            logmsg("job: cam%d: history not written: %s", cam, strerror(errno));
        snprintf(what, sizeof(what), "job: cam%d new: history written%s", cam, sc == SCENE_STORE ? " (store)" : "");
        step(what);
        return state;
    }
    sim_apply(sc, hist);                     /* the test override on the stored image, in memory only */
    struct imgdiff_cfg cfg;
    struct imgdiff_result r;
    imgdiff_default_cfg(&cfg);
    imgdiff_compare(hist, cur, &cfg, &r);
    state = r.changed ? "changed" : "same";
    snprintf(what, sizeof(what), "job: cam%d %s (stored %s): %d blocks, max %.1f, gain %.2f", cam, state,
             sc == SCENE_OBJECT ? "+object" : sc == SCENE_LIGHT ? "darker" : "as is", r.changed_blocks,
             (double)r.max_block_mad, (double)r.gain);
    step(what);
    return state;
}

static void *job_thread(void *arg)
{
    (void)arg;
    mkdir(DIR, 0755);                      /* EEXIST after the first boot */
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!pending)
            pthread_cond_wait(&cv, &mu);
        pending = 0;                       /* requests during a check: one more check after it */
        pthread_mutex_unlock(&mu);
        step("job: check");
        capture();
        {   /* each camera's mean brightness: a dark room, a covered lens or a capture that gave zeros shows here */
            char what[80];
            unsigned mean[CAMS] = { 0 };
            for (int c = 0; c < CAMS; c++)
                if (got[c]) {
                    unsigned long sum = 0;
                    for (int i = 0; i < FRAME; i++)
                        sum += raw[c][i];
                    mean[c] = (unsigned)(sum / FRAME);
                }
            snprintf(what, sizeof(what), "job: captured, mean %u / %u (got %d %d)", mean[0], mean[1], got[0], got[1]);
            step(what);
        }
        char r[32];
        snprintf(r, sizeof(r), "%s,%s", check_cam(0), check_cam(1));
        pthread_mutex_lock(&mu);
        memcpy(result, r, sizeof(result));
        result_ready = true;
        pthread_mutex_unlock(&mu);
        step("job: done");
    }
    return NULL;
}

void job_start(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, job_thread, NULL) != 0)
        logmsg("job: thread not started");
}

void job_request(const char *reason)
{
    logmsg("job: CHECK %s", reason);
    pthread_mutex_lock(&mu);
    pending++;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

bool job_result(char *buf, size_t len)
{
    pthread_mutex_lock(&mu);
    bool got = result_ready;
    if (got)
        snprintf(buf, len, "%s", result);
    result_ready = false;
    pthread_mutex_unlock(&mu);
    return got;
}
