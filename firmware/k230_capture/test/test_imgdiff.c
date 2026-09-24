/* Host tests for the change detector and the reference store, on synthetic fisheye frames. */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imgdiff.h"
#include "refstore.h"

#define W 1280
#define H 960
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned seed = 1;
static int noise(int amp) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) % (2u * amp + 1)) - amp; }

/* a scene: dark corners outside the image circle, a gradient floor, two "objects" */
static float circle_px = 790.0f;   /* image-circle radius of the synthetic lens, full-res pixels */

static void scene(uint8_t *img, float gain, int noise_amp, int blob_x, int blob_y, int blob_r, int blob_d)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = x - W / 2.0f, dy = y - H / 2.0f;
            float v = (dx * dx + dy * dy < circle_px * circle_px) ? 60.0f + 80.0f * y / H : 1.0f;
            if ((x / 160 + y / 120) % 5 == 0) v += 30;                 /* furniture */
            if (blob_r && (x - blob_x) * (x - blob_x) + (y - blob_y) * (y - blob_y) < blob_r * blob_r)
                v += blob_d;                                            /* the change */
            int p = (int)lroundf(v * gain) + noise(noise_amp);
            img[y * W + x] = (uint8_t)(p < 0 ? 0 : p > 255 ? 255 : p);
        }
}

static float mask_radius = 0.0f;   /* 0 = detector default */

static struct imgdiff_result run(const uint8_t *a, const uint8_t *b)
{
    uint8_t ra[IMGDIFF_W * IMGDIFF_H], rb[IMGDIFF_W * IMGDIFF_H];
    imgdiff_reduce(a, W, H, W, ra);
    imgdiff_reduce(b, W, H, W, rb);
    struct imgdiff_cfg cfg;
    imgdiff_default_cfg(&cfg);
    if (mask_radius > 0)
        cfg.circle_radius = mask_radius;
    struct imgdiff_result r;
    imgdiff_compare(ra, rb, &cfg, &r);
    return r;
}

int main(void)
{
    uint8_t *ref = malloc(W * H), *cur = malloc(W * H);
    struct imgdiff_result r;

    scene(ref, 1.0f, 6, 0, 0, 0, 0);
    scene(cur, 1.0f, 6, 0, 0, 0, 0);
    r = run(ref, cur);
    CHECK(!r.changed, "same scene with sensor noise flagged (blocks %d, frac %.4f)", r.changed_blocks, r.changed_frac);
    printf("same scene, noise +-6:        changed=%d blocks=%2d max_mad=%5.1f frac=%.4f\n", r.changed, r.changed_blocks, r.max_block_mad, r.changed_frac);

    scene(cur, 0.85f, 6, 0, 0, 0, 0);
    r = run(ref, cur);
    CHECK(!r.changed, "15 %% darker exposure flagged (gain %.3f, blocks %d)", r.gain, r.changed_blocks);
    printf("15 %% darker exposure:         changed=%d blocks=%2d max_mad=%5.1f gain=%.3f\n", r.changed, r.changed_blocks, r.max_block_mad, r.gain);

    scene(cur, 1.0f, 6, 700, 600, 45, -45);
    r = run(ref, cur);
    CHECK(r.changed, "puddle 90 px across not detected (max_mad %.1f)", r.max_block_mad);
    printf("dark puddle 90 px across:     changed=%d blocks=%2d max_mad=%5.1f frac=%.4f\n", r.changed, r.changed_blocks, r.max_block_mad, r.changed_frac);

    /* smaller lens circle (600 px) so the corners are really outside it; mask to match */
    circle_px = 600.0f;
    mask_radius = 600.0f / W;
    scene(ref, 1.0f, 6, 0, 0, 0, 0);
    scene(cur, 1.0f, 6, 60, 40, 40, 120);
    r = run(ref, cur);
    circle_px = 790.0f;
    mask_radius = 0.0f;
    scene(ref, 1.0f, 6, 0, 0, 0, 0);
    CHECK(!r.changed, "change in the dark corner outside the image circle flagged (blocks %d)", r.changed_blocks);
    printf("change outside image circle:  changed=%d blocks=%2d max_mad=%5.1f\n", r.changed, r.changed_blocks, r.max_block_mad);

    /* reference store: round trip, then a torn file must be rejected */
    char dir[] = "/tmp/leakcam-refXXXXXX";
    CHECK(mkdtemp(dir) != NULL, "mkdtemp");
    uint8_t a[IMGDIFF_W * IMGDIFF_H], b[IMGDIFF_W * IMGDIFF_H];
    imgdiff_reduce(ref, W, H, W, a);
    time_t t = 0;
    CHECK(refstore_load(dir, 0, REF_LAST, b, &t) == 1, "missing reference not reported as absent");
    CHECK(refstore_save(dir, 0, REF_LAST, a, 1758700000) == 0, "save");
    CHECK(refstore_load(dir, 0, REF_LAST, b, &t) == 0 && !memcmp(a, b, sizeof(a)) && t == 1758700000, "round trip");
    /* two slots: the second save goes to slot 1, the newest copy wins */
    uint8_t c[IMGDIFF_W * IMGDIFF_H];
    memcpy(c, a, sizeof(c));
    c[0] ^= 0xFF;
    CHECK(refstore_save(dir, 0, REF_LAST, c, 1758700600) == 0, "second save");
    CHECK(refstore_load(dir, 0, REF_LAST, b, &t) == 0 && !memcmp(b, c, sizeof(c)) && t == 1758700600,
          "newest slot not chosen");
    /* newest slot torn (power cut mid-write): fall back to the older good copy */
    char path[300];
    snprintf(path, sizeof(path), "%s/cam0.last.1", dir);
    CHECK(truncate(path, 1000) == 0, "truncate");
    CHECK(refstore_load(dir, 0, REF_LAST, b, &t) == 0 && !memcmp(a, b, sizeof(a)) && t == 1758700000,
          "torn newest slot not skipped");
    /* the next save overwrites the torn slot, not the good one */
    CHECK(refstore_save(dir, 0, REF_LAST, c, 1758701200) == 0, "third save");
    CHECK(refstore_load(dir, 0, REF_LAST, b, &t) == 0 && t == 1758701200, "save after tear");
    snprintf(path, sizeof(path), "%s/cam0.last.0", dir);
    CHECK(truncate(path, 1000) == 0, "truncate 0");
    snprintf(path, sizeof(path), "%s/cam0.last.1", dir);
    CHECK(truncate(path, 1000) == 0, "truncate 1");
    CHECK(refstore_load(dir, 0, REF_LAST, b, &t) == 1, "both slots torn, still accepted");
    for (int s = 0; s < 2; s++) {
        snprintf(path, sizeof(path), "%s/cam0.last.%d", dir, s);
        unlink(path);
    }
    rmdir(dir);

    printf(fails ? "imgdiff: %d failures\n" : "imgdiff: noise and exposure ignored, puddle found, mask, two-slot references and torn files handled\n", fails);
    free(ref);
    free(cur);
    return fails != 0;
}
