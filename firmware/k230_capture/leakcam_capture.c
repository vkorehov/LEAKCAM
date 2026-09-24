/*
 * leakcam_capture: one shot from both fisheye cameras, compared with what they saw before.
 *
 *   1. the white and IR LED chains on, each with its own PWM brightness (white: GPIO61 = PWM1 ->
 *      U14 CTRL -> J1, IR: GPIO60 = PWM0 -> U15 CTRL -> J2; each chain spans both cameras),
 *      10 ms for the TPS61161 soft start (32 x 213 us);
 *   2. both cameras streamed together, `settle` frames dropped for AE/AWB, one frame kept;
 *   3. LEDs off;
 *   4. each frame reduced to 320x240 and compared with cam<N>.last and cam<N>.base on the NAND;
 *   5. history (history.h): the full-resolution blocks that differ from what the stored history
 *      shows go to the NAND as a delta, or the whole frame as a keyframe;
 *   6. references updated atomically; JSON lines on stdout (per camera: vs last, vs base, history).
 *
 * The K230 loses its RAM at every power-off: everything the next wake needs is in these files.
 *
 * Exit status: 0 no change, 10 change on any camera, 1 error. Everything must be written before
 * the agent's HALTED: the BL616 cuts power right after it.
 *
 *   leakcam_capture [-n 0,3] [-s 1280x960] [-f 12] [-d /var/lib/leakcam] [-p 0] [-b white,ir]
 *                   [-q quota_mb] [--no-led] [--rebaseline] [--keyframe] [--pgm]
 *
 * --keyframe stores a whole frame whatever changed; --pgm also writes the current frame as
 * cam<N>-<time>.pgm (debug; leakcam_hist rebuilds any stored frame).
 *
 * Brightness: -b white%,ir% (0 leaves that chain off, e.g. -b 0,100 for IR only). The TPS61161
 * turns the CTRL duty cycle into a DC LED current (it chops its 200 mV reference and filters it:
 * I_LED = duty x 200 mV / R_FB), so there is no PWM flicker for the rolling-shutter OV5647 to
 * band on. White J1 (R66 3.0 ohm) 0..66.7 mA, IR J2 (R68 1.5 ohm) 0..133 mA; near 100 % the
 * driver runs into its switch current limit and delivers what it can.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "history.h"
#include "imgdiff.h"
#include "led.h"
#include "refstore.h"
#include "cap.h"

#ifdef LEAKCAM_RTSMART
static const char *state_dir = "/sdcard/leakcam";    /* UFFS on NAND partition nand1 */
#else
static const char *state_dir = "/var/lib/leakcam";
#endif
static unsigned pwm_chip = 0;                        /* K230 pwm0 controller: PWM0..PWM2 */
static unsigned led_percent[2] = { 100, 100 };

static void print_result(unsigned slot, const char *against, const struct imgdiff_result *r, bool have)
{
    if (!have) {
        printf("{\"cam\":%u,\"vs\":\"%s\",\"reference\":false}\n", slot, against);
        return;
    }
    printf("{\"cam\":%u,\"vs\":\"%s\",\"changed\":%s,\"blocks\":%d,\"max_block_mad\":%.1f,"
           "\"changed_frac\":%.4f,\"mean_ref\":%.1f,\"mean_new\":%.1f,\"gain\":%.3f}\n",
           slot, against, r->changed ? "true" : "false", r->changed_blocks, r->max_block_mad,
           r->changed_frac, r->mean_ref, r->mean_new, r->gain);
}

static void print_history(unsigned slot, const struct hist_info *h)
{
    static const char *const kind[] = { [HIST_NONE] = "none", [HIST_KEY] = "keyframe",
                                        [HIST_DELTA] = "delta" };
    printf("{\"cam\":%u,\"history\":\"%s\",\"seq\":%u,\"key_seq\":%u,\"blocks\":%u,"
           "\"bytes\":%zu,\"pruned\":%u}\n",
           slot, kind[h->kind], h->seq, h->key_seq, h->nblocks, h->bytes, h->pruned);
}

/* copy the changed blocks of the reduced frame into the reduced history view */
static void apply_blocks(uint8_t *hist, const uint8_t *cur,
                         const uint8_t changed[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X])
{
    enum { BW = IMGDIFF_W / IMGDIFF_BLOCKS_X, BH = IMGDIFF_H / IMGDIFF_BLOCKS_Y };
    for (int by = 0; by < IMGDIFF_BLOCKS_Y; by++)
        for (int bx = 0; bx < IMGDIFF_BLOCKS_X; bx++)
            if (changed[by][bx])
                for (int y = by * BH; y < (by + 1) * BH; y++)
                    memcpy(hist + y * IMGDIFF_W + bx * BW, cur + y * IMGDIFF_W + bx * BW, BW);
}

int main(int argc, char **argv)
{
    unsigned nodes[CAP_MAX_CAMS] = CAP_DEFAULT_NODES;
    int ncam = 2;
    unsigned width = 1280, height = 960;      /* OV5647 2x2 binned, full field of view */
    unsigned settle = 12;                     /* ~0.3 s at 45 fps */
    unsigned quota_mb = 0;                    /* 0 = history default */
    bool use_led = true, rebaseline = false, force_key = false, save_pgm = false;
    struct hist_cfg hcfg;

    static const struct option lopt[] = {
        { "no-led", no_argument, NULL, 'L' },
        { "rebaseline", no_argument, NULL, 'B' },
        { "keyframe", no_argument, NULL, 'K' },
        { "pgm", no_argument, NULL, 'P' },
        { "brightness", required_argument, NULL, 'b' },
        { 0 },
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "n:s:f:d:p:b:q:", lopt, NULL)) != -1) {
        switch (opt) {
        case 'n': {
            ncam = 0;
            for (char *t = strtok(optarg, ","); t && ncam < CAP_MAX_CAMS; t = strtok(NULL, ","))
                nodes[ncam++] = (unsigned)strtoul(t, NULL, 10);
            break;
        }
        case 's': if (sscanf(optarg, "%ux%u", &width, &height) != 2) return 2; break;
        case 'f': settle = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'd': state_dir = optarg; break;
        case 'p': pwm_chip = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'b':
            if (sscanf(optarg, "%u,%u", &led_percent[LED_WHITE], &led_percent[LED_IR]) != 2 ||
                led_percent[LED_WHITE] > 100 || led_percent[LED_IR] > 100)
                return 2;
            break;
        case 'L': use_led = false; break;
        case 'B': rebaseline = true; break;
        case 'K': force_key = true; break;
        case 'P': save_pgm = true; break;
        case 'q': quota_mb = (unsigned)strtoul(optarg, NULL, 10); break;
        default:
            fprintf(stderr, "usage: %s [-n 0,3] [-s WxH] [-f settle] [-d dir] [-p pwmchip] "
                            "[-b white%%,ir%%] [-q quota_mb] [--no-led] [--rebaseline] "
                            "[--keyframe] [--pgm]\n", argv[0]);
            return 2;
        }
    }
    if (mkdir(state_dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", state_dir, strerror(errno));
        return 1;
    }
    hist_default_cfg(&hcfg, state_dir);
    if (quota_mb)
        hcfg.quota_bytes = (size_t)quota_mb << 20;

    struct cap_cam cams[CAP_MAX_CAMS];
    memset(cams, 0, sizeof(cams));
    for (int i = 0; i < ncam; i++)
        cams[i].node = nodes[i];

    int status = 1;
    if (cap_open_all(cams, ncam, width, height) < 0)
        goto out;
    if (use_led && led_set(true, led_percent, pwm_chip) < 0)
        fprintf(stderr, "led: continuing without full illumination\n");
    int grab = cap_grab_all(cams, ncam, settle, 5000);
    if (use_led)
        led_set(false, led_percent, pwm_chip);    /* duty 0 + disable: CTRL low, drivers shut down */
    if (grab < 0)
        goto out;

    time_t now = time(NULL);
    bool any_change = false;
    status = 0;
    for (int i = 0; i < ncam; i++) {
        unsigned slot = cams[i].slot;
        uint8_t cur[IMGDIFF_W * IMGDIFF_H], ref[IMGDIFF_W * IMGDIFF_H];
        if (imgdiff_reduce(cams[i].luma, (int)cams[i].width, (int)cams[i].height,
                           (int)cams[i].width, cur) < 0) {
            fprintf(stderr, "cam %u: %ux%u is not a multiple of %dx%d\n", slot, cams[i].width,
                    cams[i].height, IMGDIFF_W, IMGDIFF_H);
            status = 1;
            continue;
        }
        struct imgdiff_cfg cfg;
        imgdiff_default_cfg(&cfg);
        struct imgdiff_result r;
        bool changed = false;

        bool have_last = refstore_load(state_dir, (int)slot, REF_LAST, ref, NULL) == 0;
        if (have_last) {
            imgdiff_compare(ref, cur, &cfg, &r);
            changed |= r.changed;
        }
        print_result(slot, "last", &r, have_last);

        bool have_base = !rebaseline && refstore_load(state_dir, (int)slot, REF_BASE, ref, NULL) == 0;
        if (have_base) {
            imgdiff_compare(ref, cur, &cfg, &r);
            changed |= r.changed;
        }
        print_result(slot, "base", &r, have_base);

        /* history: the blocks that differ from what the stored history shows. Written before
         * cam<N>.hist, so a power cut in between only makes the next wake store them again. */
        uint8_t hist[IMGDIFF_W * IMGDIFF_H];
        struct imgdiff_result hr;
        struct hist_info hi;
        bool have_hist = !rebaseline && !force_key &&
                         refstore_load(state_dir, (int)slot, REF_HIST, hist, NULL) == 0;
        if (have_hist)
            imgdiff_compare(hist, cur, &cfg, &hr);
        if (hist_store(&hcfg, (int)slot, now, cams[i].luma, cams[i].width, cams[i].height,
                       have_hist ? (const uint8_t (*)[IMGDIFF_BLOCKS_X])hr.block_changed : NULL,
                       have_hist ? hr.usable_blocks : 0, !have_hist, &hi) < 0) {
            fprintf(stderr, "cam %u: storing history failed: %s\n", slot, strerror(errno));
            status = 1;
        } else {
            print_history(slot, &hi);
            if (hi.kind == HIST_KEY)
                memcpy(hist, cur, sizeof(hist));
            else if (hi.kind == HIST_DELTA)
                apply_blocks(hist, cur, (const uint8_t (*)[IMGDIFF_BLOCKS_X])hr.block_changed);
            if (hi.kind != HIST_NONE && refstore_save(state_dir, (int)slot, REF_HIST, hist, now) < 0)
                fprintf(stderr, "cam %u: saving the history view failed: %s\n", slot, strerror(errno));
        }

        if (save_pgm) {
            char path[256];
            struct tm tm;
            gmtime_r(&now, &tm);
            snprintf(path, sizeof(path), "%s/cam%u-%04d%02d%02dT%02d%02d%02dZ.pgm", state_dir, slot,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            if (hist_write_pgm(path, cams[i].luma, cams[i].width, cams[i].height) < 0)
                fprintf(stderr, "cam %u: saving %s failed\n", slot, path);
        }
        if (refstore_save(state_dir, (int)slot, REF_LAST, cur, now) < 0 ||
            (!have_base && refstore_save(state_dir, (int)slot, REF_BASE, cur, now) < 0)) {
            fprintf(stderr, "cam %u: saving references failed: %s\n", slot, strerror(errno));
            status = 1;
        }
        any_change |= changed;
    }
    if (status == 0 && any_change)
        status = 10;
out:
    cap_close_all(cams, ncam);
    return status;
}
