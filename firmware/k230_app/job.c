/*
 * The leak detection job (DESIGN.md 5.3.8, 5.3.12), cameras simulated until they arrive (user, 2026-11-07).
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
 * The simulated cameras: a fixed synthetic room per camera (gradient, furniture, sensor noise). /sdcard/leakcam/sim
 * picks each camera's scene: the last `cam<N>=<scene>` in it counts, so the K230 console's `echo` (which appends and
 * cannot create a file) sets it: `echo cam0=object /sdcard/leakcam/sim`; `rm` it to go back to same (the job creates
 * it empty again). Scenes: same (default), light (the room 40 % darker) or object (a dark puddle on the floor).
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

#include "imgdiff.h"
#include "leakcam.h"

#define CAMS     2
#define DIR      "/sdcard/leakcam"
#define SIM_FILE DIR "/sim"
#define FRAME    (IMGDIFF_W * IMGDIFF_H)

enum scene { SCENE_SAME, SCENE_LIGHT, SCENE_OBJECT };

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static unsigned pending = 1;            /* the check at the start */
static char result[32];
static bool result_ready;
static uint8_t raw[FRAME], cur[FRAME], hist[FRAME];

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
    return !strncmp(p, "light", 5) ? SCENE_LIGHT : !strncmp(p, "object", 6) ? SCENE_OBJECT : SCENE_SAME;
}

/* the simulated camera: the room of camera `cam` in `sc`, a new noise pattern every time */
static void sim_capture(int cam, enum scene sc, uint8_t *out)
{
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            int v = 40 + x * 2 / 5 + y / 5;                                   /* light from one side */
            if (x > 30 + cam * 40 && x < 110 + cam * 40 && y > 40 && y < 140)
                v += 70;                                                      /* a cabinet */
            if (x > 200 && x < 290 - cam * 20 && y > 30 && y < 90)
                v -= 25;                                                      /* a window frame */
            if (sc == SCENE_OBJECT) {
                int dx = x - 190, dy = y - 180;
                if (dx * dx * 9 + dy * dy * 49 < 40 * 40 * 9)
                    v = 22;                                                   /* water on the floor */
            }
            if (sc == SCENE_LIGHT)
                v = v * 6 / 10;
            v += rand() % 5 - 2;                                              /* sensor noise */
            out[y * IMGDIFF_W + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
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
    char path[48], what[96];
    enum scene sc = sim_scene(cam);
    sim_capture(cam, sc, raw);
    imgdiff_reduce(raw, IMGDIFF_W, IMGDIFF_H, IMGDIFF_W, cur);         /* the 3x3 smoothing of a real capture */
    hist_path(cam, path, sizeof(path));
    const char *state;
    if (!load(path, hist)) {
        state = "new";
        if (!save(path, cur))
            logmsg("job: cam%d: history not written: %s", cam, strerror(errno));
    } else {
        struct imgdiff_cfg cfg;
        struct imgdiff_result r;
        imgdiff_default_cfg(&cfg);
        imgdiff_compare(hist, cur, &cfg, &r);
        state = r.changed ? "changed" : "same";
        snprintf(what, sizeof(what), "job: cam%d %s (simulated %s): %d blocks, max %.1f, gain %.2f", cam, state,
                 sc == SCENE_OBJECT ? "object" : sc == SCENE_LIGHT ? "light" : "same", r.changed_blocks,
                 (double)r.max_block_mad, (double)r.gain);
        step(what);
        return state;
    }
    snprintf(what, sizeof(what), "job: cam%d new: history written", cam);
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
