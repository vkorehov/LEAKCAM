/*
 * The leak detection job (DESIGN.md 5.3.8, 5.3.12) on the two real cameras (vicap_cap.c; until 2026-11-12 the cameras
 * were simulated, user 2026-11-07).
 *
 * A check: per camera the native 2592x1936 luma frame (user 2026-11-12: "low resolution will not see any leak"), read in
 * place and compared with that camera's history (imgdiff: 16x16 blocks, gain-normalised, so a change of the light
 * level alone reads as the same scene):
 *   new      no history yet: the frame becomes the history
 *   same     nothing changed
 *   changed  a change imgdiff sees; the history is kept (it changes only once the server has seen the change and said
 *            "no leak", a later step)
 *   light    (the change net's answer for a change of the light only: not built yet)
 * The history is /sdcard/leakcam/cam<N>.hist (cam<N>.amb.hist in the room's light), the raw native luma (5 MB).
 *
 * Capture: both cameras at once (offline VICAP, the native 2592x1944; the streams run while the cameras are open,
 * vicap_cap.c), the room's light when it is enough, else the LEDs; the exposure ours (cap.h: each camera's last good
 * one, corrected when the frame is off), right in 2-3 frames (at most SETTLE; the ISP's AE took 14-40); only the light
 * probe reads the ISP's 320x240 channel. A camera that gives no frame reads "nocam", one whose picture is nearly
 * black "dark" (JOB_DARK_MEAN).
 *
 * The test override (user 2026-11-12: "update the stored one through a special command"; the new image still comes
 * from the camera and so differs): /sdcard/leakcam/sim, the last `cam<N>=<token>` per camera counts, so the K230
 * console's `echo` (which appends and cannot create a file) sets it, `rm` ends it (the job creates it empty again).
 * The tokens change only the stored image the real frame is compared with, in memory (the file stays the camera's):
 *   object  a puddle painted into it (brightness inverted there): the real frame reads "changed"
 *   light   it 40 % darker: imgdiff's gain normalisation must still read "same"
 *   store   the real frame replaces the stored image (a new baseline, written to the file)
 *   dark    the camera reads "dark" whatever it sees (tests/test149: DESIGN.md 5.3.18)
 *   curve   (any camera) every grab takes all SETTLE + 1 frames and logs each one's brightness: the exposure curve
 *   mix=white / mix=ir / mix=both / mix=none   (any camera) which LED chains a lit shot uses (tests/test145: what
 *           the IR chain adds through the lenses' IR-cut filters)
 *   lit=led / lit=room   (any camera) the lighting forced: the LEDs, or the room's light alone at up to the longest
 *           exposure (tests/test144: one scene both ways)
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
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "cap.h"
#include "focus.h"
#include "wb.h"
#include "imgdiff.h"
#include "leakcam.h"
#include "led.h"

#define CAMS     JOB_CAMS
#define DIR      "/sdcard/leakcam"
#define SIM_FILE DIR "/sim"
#define FRAME    (IMGDIFF_W * IMGDIFF_H)   /* = JOB_W x JOB_H */

#define CAP_W    JOB_W                  /* the OV5647 binned mode (vicap_cap.c) */
#define CAP_H    JOB_H
#define LIT_FRAMES 2                    /* frames to pass after the LEDs went on before one is kept: a camera on a long
                                         * exposure kept its 2nd frame, exposed mostly before the light (2026-11-14:
                                         * J4's wall yellow with the LED white balance, right with the room's) */
#define WB_FIRST_FRAMES 14              /* a lit shot's frames before the kept one while a camera has no kept LED white
                                         * balance yet (wb.c learns after 12) */
#define SETTLE   20                     /* the most frames before the kept one (1.3 s at 15 fps): our exposure is
                                         * right in 3 (cap.h), each correction 3 more; the ISP's own AE took 14-40 */

/* the LEDs for a lit shot: white at full, IR off. Measured 2026-11-12 (the same scene and exposure, mean of 255):
 * room light 67 / 23 (cam0 / cam1), IR alone 68 / 25, white + IR 96 / 79: the lenses' IR-cut filters block the IR
 * chain's light, its ~1.8 W was waste (user: "do we need IR light at all?"). Back on with IR-passing lenses */
static unsigned led_percent[2] = { [LED_WHITE] = 100, [LED_IR] = 0 };
static unsigned led_ms, frames_used[JOB_CAMS];   /* the last grab: LEDs on for, frames per camera */
static unsigned tries_used[JOB_CAMS], ambient_seen[JOB_CAMS];
static bool shot_lit[JOB_CAMS];                 /* the last shot: with the LEDs, or in the room's light */
static float shot_exp[JOB_CAMS];
static struct timespec shot_at;                 /* the last shot's moment (CLOCK_REALTIME) */
/* each camera's last good exposure (time x gain, s; cap.h): the next shot starts from it, so it is usually right at
 * once (2-3 frames with the LEDs on); and the exposure each sensor was left at (the last shot's, LEDs or room light):
 * the cameras open at it, so the light probe after a boot reads the room as the running program would (user
 * 2026-11-12, "add persistence": a cold probe at the 10 ms default read 14 where a warm one read 75). In EXP_FILE for
 * the next boot (on battery every session is a cold start), written when one moved by more than 10 % */
#define EXP_FILE "/sdcard/leakcam/exposure"
#define SENSOR(c) (JOB_CAMS + (c))              /* exposure[]: [cam] the LED shots', [SENSOR(cam)] the sensor's last */
static float exposure[2 * JOB_CAMS], exposure_saved[2 * JOB_CAMS];

static void exposure_load(void)
{
    FILE *f = fopen(EXP_FILE, "r");
    if (f) {
        if (fscanf(f, "%f %f %f %f", &exposure[0], &exposure[1], &exposure[2], &exposure[3]) < 2)
            memset(exposure, 0, sizeof(exposure));
        fclose(f);
    }
    memcpy(exposure_saved, exposure, sizeof(exposure));
}

static void exposure_keep(void)
{
    bool moved = false;
    for (int i = 0; i < 2 * JOB_CAMS; i++)
        if (exposure[i] > 0 && (exposure_saved[i] <= 0 || fabsf(exposure[i] / exposure_saved[i] - 1.0f) > 0.10f))
            moved = true;
    if (!moved)
        return;
    FILE *f = fopen(EXP_FILE, "w");
    if (!f)
        return;
    fprintf(f, "%f %f %f %f\n", exposure[0], exposure[1], exposure[2], exposure[3]);
    fclose(f);
    memcpy(exposure_saved, exposure, sizeof(exposure));
}

static void sim_curve(void);
static int sim_lit;                             /* `lit=led` 1, `lit=room` -1, else 0: the lighting forced */
static unsigned sim_pct[2];                     /* `mix=white|ir|none`: the chains for this shot (else led_percent) */
static bool sim_mix;

/* Light first, LEDs only when it is too dark (user 2026-11-12: "currently you are flashing even if light already
 * present"): one frame with the LEDs off says what the room's light gives at the exposure in use; when the target
 * needs at most CAP_AMBIENT_MAX the shot is taken in it, the LEDs stay off, else with the LEDs at the last good LED
 * exposure. Any camera too dark: the LEDs for both (they light both). shot_lit[] says which, the history follows it */
static int grab_lit(struct cap_cam *cams, int n, cap_frame_fn fn, void *ctx, unsigned more, unsigned more_ms)
{
    sim_curve();
    struct timespec t0, t1;
    bool probed = cap_ambient(cams, n, 1000) == 0;
    bool leds = sim_lit ? sim_lit > 0 : cap_curve || !probed;   /* no probe frame: the safe way, lit */
    for (int i = 0; i < n && !leds; i++) {
        unsigned m = cams[i].ambient;
        float need = m < CAP_BLACK ? 1e9f : cams[i].ambient_exp * powf((float)CAP_TARGET / (float)m, 2.0f);
        if (need > CAP_AMBIENT_MAX && sim_lit >= 0)
            leds = true;
        else                                    /* the room's light: this exposure, corrected by the grab */
            cams[i].exp = need < CAP_T_MAX * CAP_G_MAX ? need : CAP_T_MAX * CAP_G_MAX;
    }
    for (int i = 0; i < n; i++) {
        ambient_seen[cams[i].slot] = cams[i].ambient;
        if (leds)
            cams[i].exp = exposure[cams[i].slot];
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const unsigned *pct = sim_mix ? sim_pct : led_percent;
    if (leds && led_set(true, pct, 0) < 0)
        logmsg("job: LEDs not on: the shot without full illumination");
    /* the white balance under the LEDs: each camera's kept gains from the first frame (wb.c); a camera without them
     * yet: frames for the automatic to follow, enough for it to settle and be learnt this once */
    wb_light(leds);
    cap_light_frames = !leds ? 0 : wb_known() ? LIT_FRAMES : WB_FIRST_FRAMES;
    int rc = cap_grab_all(cams, n, 0, SETTLE + cap_light_frames, 1000);
    cap_light_frames = 0;
    if (leds && !more) {
        led_set(false, pct, 0);                 /* before the frames are worked on (a native JPEG: ~1 s) */
        wb_light(false);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    clock_gettime(CLOCK_REALTIME, &shot_at);    /* the moment, for the pictures' X-Taken and EXIF */
    led_ms = leds ? (unsigned)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000) : 0;
    for (int i = 0; i < n; i++) {
        frames_used[cams[i].slot] = cams[i].frames;
        tries_used[cams[i].slot] = cams[i].tries;
        shot_lit[cams[i].slot] = leds;
        shot_exp[cams[i].slot] = cams[i].exp;
        if (rc == 0 && leds)
            exposure[cams[i].slot] = cams[i].exp;   /* the LED exposure: the next lit shot starts from it */
        if (rc == 0)
            exposure[SENSOR(cams[i].slot)] = cams[i].exp;   /* where the sensor stays: the next probe's */
        if (cap_curve) {                        /* the test's curve: every frame's brightness */
            char t[3 * 64 + 1];
            size_t k = 0;
            for (unsigned f = 0; f < cams[i].frames && f < sizeof(cams[i].trace); f++)
                k += (size_t)snprintf(t + k, sizeof(t) - k, "%u ", cams[i].trace[f]);
            logmsg("job: curve cam%u: %s", cams[i].slot, t);
        }
    }
    if (rc == 0) {
        exposure_keep();
        wb_keep();
    }
    if (cap_release_all(cams, n, rc == 0 ? fn : NULL, ctx) != 0)
        rc = -1;
    if (more && rc == 0)                        /* a video: the frames after the first, lit the same way */
        rc = cap_stream(cams, n, more, more_ms, 0, fn, ctx, 1000);
    if (leds && more) {
        led_set(false, pct, 0);
        wb_light(false);
        wb_keep();                              /* a video's frames taught the LED white balance */
    }
    return rc;
}

enum scene { SCENE_SAME, SCENE_LIGHT, SCENE_OBJECT, SCENE_STORE, SCENE_DARK };

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static unsigned pending = 1;            /* the check at the start */
static char result[48];
static bool result_ready;
static uint8_t *hist;                   /* a camera's history, loaded for its compare: 5 MB only during a check
                                         * (the VPU's pages come from the same allocator: a video needs them) */
#define JOB_DARK_MEAN 16                /* a kept frame's mean brightness (0-255) below it: "dark", an unreliable
                                         * capture, not a leak (user 2026-11-13; DESIGN.md 5.3.18); the exposure aims
                                         * at CAP_TARGET 90, a black frame is below CAP_BLACK 4 */
static const char *state[CAMS];         /* this check's verdict per camera (check_frame), NULL: no frame */
static unsigned ignored(void);          /* the mask of the cameras the person ignores (below) */

/* the test token `curve` in the scene file: every grab takes all SETTLE + 1 frames and logs each frame's brightness
 * (the brightness after the LEDs went on, at the stored exposure: for tuning cap.h's CAP_*); off without it */
static void sim_curve(void)
{
    char buf[256] = { 0 };
    int fd = open(SIM_FILE, O_RDONLY);
    if (fd >= 0) {
        (void)!read(fd, buf, sizeof(buf) - 1);
        close(fd);
    }
    cap_curve = strstr(buf, "curve") != NULL;
    const char *l = NULL;
    for (const char *q = buf; (q = strstr(q, "lit=")) != NULL; q++)
        l = q;                                  /* the last one counts */
    sim_lit = !l ? 0 : !strncmp(l + 4, "led", 3) ? 1 : !strncmp(l + 4, "room", 4) ? -1 : 0;
    const char *x = NULL;
    for (const char *q = buf; (q = strstr(q, "mix=")) != NULL; q++)
        x = q;
    sim_mix = x != NULL;
    sim_pct[LED_WHITE] = x && (!strncmp(x + 4, "white", 5) || !strncmp(x + 4, "both", 4)) ? 100 : 0;
    sim_pct[LED_IR] = x && (!strncmp(x + 4, "ir", 2) || !strncmp(x + 4, "both", 4)) ? 100 : 0;
}

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
           !strncmp(p, "store", 5) ? SCENE_STORE : !strncmp(p, "dark", 4) ? SCENE_DARK : SCENE_SAME;
}

/* the test override on the stored image (the file header above): the puddle or the darker room, in memory */
static void sim_apply(enum scene sc, uint8_t *img)
{
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            uint8_t *v = &img[y * IMGDIFF_W + x];
            if (sc == SCENE_OBJECT) {       /* the puddle where it was on the 320x240 frame: ~320x150 pixels */
                int dx = x * 320 / IMGDIFF_W - 190, dy = y * 240 / IMGDIFF_H - 180;
                if (dx * dx * 9 + dy * dy * 49 < 40 * 40 * 9)
                    *v = *v < 128 ? 230 : 22;   /* a puddle that always differs from the real frame: bright on a
                                                 * dark floor, dark on a light one (a fixed 22 matched a dark room) */
            } else if (sc == SCENE_LIGHT) {
                *v = (uint8_t)(*v * 6 / 10);
            }
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
    static bool loaded;
    if (!loaded) {
        exposure_load();
        wb_load();
        loaded = true;
    }
    memset(cams, 0, sizeof(cams));
    for (int i = 0; i < CAMS; i++) {
        cams[i].node = (unsigned)i;
        cams[i].exp = exposure[SENSOR(i)];
    }
    if (cap_open_all(cams, CAMS, CAP_W, CAP_H) == 0) {
        /* the microphone with the cameras (their VB is up now): the codec's ~2 s power-up runs beside the cameras'
         * start and the check, so a video's sound starts with its pictures (a "video" request streams 0.4 s after it
         * comes, test148) */
        audio_open();
        return CAMS;
    }
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
    for (int i = 0; i < n; i++) {
        cams[i].node = ok[i];
        cams[i].exp = exposure[SENSOR(ok[i])];
    }
    if (n && cap_open_all(cams, n, CAP_W, CAP_H) != 0) {
        cap_close_all(cams, n);
        return 0;
    }
    logmsg("job: %d of %d cameras answer", n, CAMS);
    if (n)
        audio_open();
    return n;
}

/* the cameras are the leak check's and the pictures' (media.c, its own thread): one user at a time */
static pthread_mutex_t cam_mu = PTHREAD_MUTEX_INITIALIZER;

/* one lit frame of every open camera, both streaming at once; false: none (the cameras closed, opened again next
 * time). cam_mu held */
static bool grab_open(cap_frame_fn fn, void *ctx, unsigned more, unsigned more_ms)
{
    if (ncams < 0)
        ncams = open_cams();
    if (ncams <= 0)
        return false;
    if (grab_lit(cams, ncams, fn, ctx, more, more_ms) != 0) {
        audio_close();                  /* before the cameras' VB exit */
        cap_close_all(cams, ncams);
        ncams = -1;                     /* opened again at the next check */
        return false;
    }
    return true;
}

static void check_frame(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride,
                        const void *frame, void *ctx);

/* one frame of each open camera, each compared in place with its history (state[] says how; NULL: no frame) */
static void capture(void)
{
    for (int i = 0; i < CAMS; i++)
        state[i] = NULL;
    hist = malloc(FRAME);
    if (!hist) {
        logmsg("job: no memory for the history (%d B)", FRAME);
        return;
    }
    pthread_mutex_lock(&cam_mu);
    grab_open(check_frame, NULL, 0, 0);
    pthread_mutex_unlock(&cam_mu);
    free(hist);
    hist = NULL;
}

void job_shot_time(struct timespec *t)
{
    *t = shot_at;
}

int job_shoot(cap_frame_fn fn, void *ctx, struct timespec *taken)
{
    return job_video(0, 0, fn, ctx, taken);
}

int job_video(unsigned more, unsigned more_ms, cap_frame_fn fn, void *ctx, struct timespec *taken)
{
    int mask = 0;
    pthread_mutex_lock(&cam_mu);
    if (ncams < 0)
        ncams = open_cams();
    bool ok = ncams > 0 && grab_open(fn, ctx, more, more_ms);
    *taken = shot_at;
    for (int i = 0; ok && i < ncams; i++)
        mask |= 1 << cams[i].slot;
    if (ok)
        logmsg("job: colour %s, room %u / %u, frames %u / %u, exposures %u / %u, %.1f / %.1f ms, LEDs %u ms",
               shot_lit[0] || shot_lit[1] ? "LEDs" : "room light", ambient_seen[0], ambient_seen[1], frames_used[0],
               frames_used[1], tries_used[0], tries_used[1], shot_exp[0] * 1000, shot_exp[1] * 1000, led_ms);
    pthread_mutex_unlock(&cam_mu);
    return mask;
}
/* one history per lighting: the LEDs' (cam<N>.hist) and the room's (cam<N>.amb.hist): a room-lit frame against an
 * LED-lit history differs in its shadows, not only its level, and would read "changed" (the change net's `light`
 * verdict, DESIGN.md 5.3.12, would let them share one) */
static bool hist_path(int cam, char *p, size_t n)
{
    return snprintf(p, n, DIR "/cam%d%s.hist", cam, shot_lit[cam] ? "" : ".amb") < (int)n;
}

static bool load(const char *path, uint8_t *buf)
{
    int fd = open(path, O_RDONLY);
    bool ok = fd >= 0 && read(fd, buf, FRAME) == FRAME;
    if (fd >= 0)
        close(fd);
    return ok;
}

static bool save(const char *path, const uint8_t *y, unsigned stride)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = fd >= 0;
    if (ok && stride == IMGDIFF_W)
        ok = write(fd, y, FRAME) == FRAME;
    for (int r = 0; ok && stride != IMGDIFF_W && r < IMGDIFF_H; r++)
        ok = write(fd, y + (size_t)r * stride, IMGDIFF_W) == IMGDIFF_W;
    if (fd >= 0)
        close(fd);
    return ok;
}

/* the leak check's cap_frame_fn: a camera's native luma, in place, against its history (cam_mu held, after the shot) */
static void check_frame(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride,
                        const void *frame, void *ctx)
{
    (void)uv;
    (void)frame;
    (void)ctx;
    int cam = (int)c->slot;
    char path[48], what[200];
    if (c->fw != IMGDIFF_W || c->fh != IMGDIFF_H)
        return;                                 /* not the native frame: "nocam" */
    if (ignored() >> cam & 1u) {                /* the person's choice: not judged, nothing stored */
        state[cam] = "ignored";
        snprintf(what, sizeof(what), "job: cam%d ignored", cam);
        step(what);
        return;
    }
    enum scene sc = sim_scene(cam);
    if (c->mean < JOB_DARK_MEAN || sc == SCENE_DARK) {   /* SCENE_DARK: the test token `cam<N>=dark` (tests/test149) */
        /* nothing to see, the exposure at its end (the LEDs on in a dark room): covered, nothing in the LEDs' reach, far
         * out of focus. Its noise against the history would read "changed" at every check (2026-11-13: mean 7,
         * 17929 of 19602 blocks, a leak alarm per check): no comparison, the history kept, the focus not judged */
        state[cam] = "dark";
        snprintf(what, sizeof(what), "job: cam%d dark: mean %u, below %u%s", cam, c->mean, JOB_DARK_MEAN,
                 sc == SCENE_DARK ? " (simulated)" : "");
        step(what);
        return;
    }
    if (shot_lit[cam])                          /* the LEDs' light, the same every time: the focus judged */
        focus_judge(cam, focus_score(y, stride, c->fw, c->fh));
    hist_path(cam, path, sizeof(path));
    struct timespec t0, t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool had = load(path, hist);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (!had || sc == SCENE_STORE) {
        state[cam] = "new";
        if (!save(path, y, stride))
            logmsg("job: cam%d: history not written: %s", cam, strerror(errno));
        snprintf(what, sizeof(what), "job: cam%d new: history written%s", cam, sc == SCENE_STORE ? " (store)" : "");
        step(what);
        return;
    }
    sim_apply(sc, hist);                     /* the test override on the stored image, in memory only */
    struct imgdiff_cfg cfg;
    static struct imgdiff_result r;          /* 19.6 KB of block flags: off the thread's stack */
    imgdiff_default_cfg(&cfg);
    imgdiff_compare(hist, y, (int)stride, &cfg, &r);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    state[cam] = r.changed ? "changed" : "same";
    int x0 = IMGDIFF_BLOCKS_X, y0 = IMGDIFF_BLOCKS_Y, x1 = -1, y1 = -1;   /* where: the changed blocks' box */
    for (int by = 0; by < IMGDIFF_BLOCKS_Y; by++)
        for (int bx = 0; bx < IMGDIFF_BLOCKS_X; bx++)
            if (r.block_changed[by][bx]) {
                x0 = bx < x0 ? bx : x0, x1 = bx > x1 ? bx : x1;
                y0 = by < y0 ? by : y0, y1 = by > y1 ? by : y1;
            }
    snprintf(what, sizeof(what), "job: cam%d %s (stored %s): %d of %d blocks at x %d-%d y %d-%d, max %.1f, gain %.2f, "
             "read %ld ms, compare %ld ms", cam, state[cam],
             sc == SCENE_OBJECT ? "+object" : sc == SCENE_LIGHT ? "darker" : "as is", r.changed_blocks,
             r.usable_blocks, x1 < 0 ? 0 : x0 * IMGDIFF_BLOCK, (x1 + 1) * IMGDIFF_BLOCK, y1 < 0 ? 0 : y0 * IMGDIFF_BLOCK,
             (y1 + 1) * IMGDIFF_BLOCK, (double)r.max_block_mad, (double)r.gain,
             (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000,
             (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000000);
    step(what);
}

/* a camera's verdict, "nocam" when it gave no frame */
static const char *check_cam(int cam)
{
    if (!state[cam]) {
        char what[48];
        snprintf(what, sizeof(what), "job: cam%d nocam: no frame", cam);
        step(what);
        return "nocam";
    }
    return state[cam];
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
            char what[224];
            unsigned mean[CAMS] = { 0 };
            for (int i = 0; i < ncams; i++)
                mean[cams[i].slot] = cams[i].mean;
            snprintf(what, sizeof(what), "job: captured, mean %u / %u (got %d %d), %s, room %u / %u, frames %u / %u, "
                     "exposures %u / %u, %.1f / %.1f ms, LEDs %u ms", mean[0], mean[1], state[0] != NULL, state[1] != NULL,
                     shot_lit[0] || shot_lit[1] ? "LEDs" : "room light", ambient_seen[0], ambient_seen[1],
                     frames_used[0], frames_used[1], tries_used[0], tries_used[1], shot_exp[0] * 1000,
                     shot_exp[1] * 1000, led_ms);
            step(what);
        }
        /* a camera out of focus (DESIGN.md 5.3.17): its same / changed not trusted; then each camera's sharpness, % of
         * its reference (-1: none yet) */
        const char *v[CAMS];
        for (int i = 0; i < CAMS; i++) {
            v[i] = check_cam(i);
            if (focus_bad(i) && strcmp(v[i], "nocam") && strcmp(v[i], "new") && strcmp(v[i], "dark") &&
                strcmp(v[i], "ignored"))
                v[i] = "unfocused";
        }
        char r[48];
        snprintf(r, sizeof(r), "%s,%s,%d,%d", v[0], v[1], focus_pct(0), focus_pct(1));
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


#define FOLLOW_EVERY 5                  /* frames between two exposure steps of a session (a step shows CAP_APPLY = 2 on) */

int job_focus(cap_frame_fn fn, void *ctx, bool (*go)(void *ctx), int (*mapped)(void *ctx))
{
    pthread_mutex_lock(&cam_mu);
    if (ncams < 0)
        ncams = open_cams();
    if (ncams <= 0) {
        pthread_mutex_unlock(&cam_mu);
        return -1;
    }
    /* the LEDs as for a lit check, the exposures settled on the first frames; then the shown camera's follows the
     * light (cap_follow below; the score is normalised by the brightness) */
    wb_light(true);
    if (led_set(true, led_percent, 0) < 0)
        logmsg("focus: LEDs not on");
    for (int i = 0; i < ncams; i++)
        cams[i].exp = exposure[cams[i].slot];
    int rc = cap_grab_all(cams, ncams, 0, SETTLE, 1000);
    if (rc == 0) {
        for (int i = 0; i < ncams; i++)
            exposure[cams[i].slot] = cams[i].exp;
        rc = cap_release_all(cams, ncams, NULL, NULL);
    }
    unsigned frames = 0;
    while (rc == 0 && go(ctx)) {
        unsigned map = 0;               /* the shown camera's luma mapped (focus.c scores a few tiles of every frame) */
        int want = mapped(ctx);
        for (int i = 0; i < ncams; i++)
            if ((int)cams[i].slot == want)
                map |= 1u << i;
        rc = cap_stream(cams, ncams, 1, 1000, map, fn, ctx, 1000);
        frames++;
        /* the shown camera's exposure follows the light all through the session (a lamp switched on, a hand at the
         * lens): a step every FOLLOW_EVERY frames, when its picture left the brightness band */
        if (rc == 0 && frames % FOLLOW_EVERY == 0)
            for (int i = 0; i < ncams; i++)
                if ((int)cams[i].slot == want && cap_follow(&cams[i]))
                    exposure[cams[i].slot] = cams[i].exp;
    }
    led_set(false, led_percent, 0);
    wb_light(false);
    wb_keep();                              /* a session's frames taught the LED white balance */
    if (rc != 0) {
        audio_close();
        cap_close_all(cams, ncams);
        ncams = -1;
    }
    pthread_mutex_unlock(&cam_mu);
    logmsg("focus: %u frame pairs, %s", frames, rc ? "camera failed" : "ended");
    return rc;
}

/* the cameras the person ignores (DESIGN.md 5.3.18): a mask kept in the K230's flash, read once */
#define IGNORE_PATH DIR "/ignore"
static int ignore_mask = -1;            /* -1: not read yet */

static unsigned ignored(void)
{
    if (ignore_mask < 0) {
        unsigned m = 0;
        FILE *f = fopen(IGNORE_PATH, "r");
        if (f) {
            if (fscanf(f, "%u", &m) != 1)
                m = 0;
            fclose(f);
        }
        ignore_mask = (int)(m & ((1u << CAMS) - 1));
    }
    return (unsigned)ignore_mask;
}

bool job_ignore(unsigned mask)
{
    mask &= (1u << CAMS) - 1;
    if (mask == ignored())
        return true;                    /* the flash written only when it changes */
    mkdir(DIR, 0755);
    FILE *f = fopen(IGNORE_PATH, "w");
    bool ok = f && fprintf(f, "%u\n", mask) > 0;
    if (f && fclose(f) != 0)
        ok = false;
    if (ok) {
        for (int cam = 0; cam < CAMS; cam++)    /* used again: its old history is of another time, "new" first */
            if ((ignored() >> cam & 1u) && !(mask >> cam & 1u))
                job_forget(cam);
        ignore_mask = (int)mask;
    }
    logmsg("job: ignored cameras 0x%x%s", mask, ok ? "" : ": NOT written");
    return ok;
}

void job_forget(int cam)
{
    char p[48];
    snprintf(p, sizeof(p), DIR "/cam%d.hist", cam);
    unlink(p);
    snprintf(p, sizeof(p), DIR "/cam%d.amb.hist", cam);
    unlink(p);
}
