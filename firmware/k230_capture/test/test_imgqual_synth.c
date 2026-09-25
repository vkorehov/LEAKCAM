/* Host tests for imgqual on synthetic 320x240 frames: histogram numbers, the verdicts against a
 * reference, the 0.35x sharpness threshold, and the LED step (direction and convergence). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgdiff.h"
#include "imgqual.h"

#define W IMGDIFF_W
#define H IMGDIFF_H
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned seed = 7;
static unsigned rnd(unsigned n) { seed = seed * 1103515245u + 12345u; return (seed >> 16) % n; }

static uint8_t clampb(float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : lroundf(v)); }

/* a sharp scene: 8x8 checker 60/190 with small random texture, like furniture edges */
static void sharp(uint8_t *img)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = (uint8_t)((((x / 8) + (y / 8)) & 1 ? 190 : 60) + (int)rnd(9) - 4);
}

/* (2r+1)^2 box blur, edges clamped: a defocused or fogged lens */
static void box(const uint8_t *src, uint8_t *dst, int r)
{
    static float tmp[W * H];

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float s = 0;
            for (int k = -r; k <= r; k++) {
                int xx = x + k < 0 ? 0 : x + k >= W ? W - 1 : x + k;
                s += src[y * W + xx];
            }
            tmp[y * W + x] = s / (2 * r + 1);
        }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float s = 0;
            for (int k = -r; k <= r; k++) {
                int yy = y + k < 0 ? 0 : y + k >= H ? H - 1 : y + k;
                s += tmp[yy * W + x];
            }
            dst[y * W + x] = clampb(s / (2 * r + 1));
        }
}

/* v -> mid + (v - mid) * k + off, clipped */
static void remap(const uint8_t *src, uint8_t *dst, float k, float off)
{
    for (int i = 0; i < W * H; i++)
        dst[i] = clampb(128 + (src[i] - 128) * k + off);
}

static struct imgqual meas(const uint8_t *img)
{
    struct imgqual q;
    imgqual_measure(img, W, H, 0, &q);
    return q;
}

static void t_numbers(void)
{
    static uint8_t img[W * H];
    struct imgqual q;

    memset(img, 100, sizeof(img));
    q = meas(img);
    CHECK(q.mean == 100 && q.p01 == 100 && q.p50 == 100 && q.p99 == 100 && q.contrast == 0 &&
          q.tenengrad == 0 && q.lapvar == 0 && q.edge_frac == 0 && q.sat_frac == 0 && q.dark_frac == 0,
          "flat frame: mean %.1f p %u/%u/%u ten %.1f", q.mean, q.p01, q.p50, q.p99, q.tenengrad);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = (uint8_t)(x * 255 / (W - 1));
    q = meas(img);
    CHECK(q.p01 <= 3 && abs(q.p50 - 127) <= 2 && q.p99 >= 251 && fabsf(q.mean - 127.3f) < 1,
          "ramp: p %u/%u/%u mean %.1f", q.p01, q.p50, q.p99, q.mean);
    CHECK(fabsf(q.sat_frac - 7.0f / W) < 0.002f && fabsf(q.dark_frac - 12.0f / W) < 0.002f,
          "ramp: sat %.4f dark %.4f", q.sat_frac, q.dark_frac);
    /* a smooth ramp (gradient 8 per pixel) is below the Sobel noise floor: no edges */
    CHECK(q.edge_frac == 0 && q.tenengrad == 0, "ramp counted as edges (%.3f)", q.edge_frac);

    /* the image circle: clipped corners outside it do not count */
    memset(img, 120, sizeof(img));
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 20; x++)
            img[y * W + x] = img[y * W + W - 1 - x] = 255;
    imgqual_measure(img, W, H, 0.45f, &q);
    CHECK(q.sat_frac == 0 && q.p99 == 120, "corners outside the circle counted (sat %.4f)", q.sat_frac);
    imgqual_measure(img, W, H, 0, &q);
    CHECK(q.sat_frac > 0.01f, "whole-frame measure missed the corners");
}

static void t_verdicts(void)
{
    static uint8_t ref[W * H], cur[W * H];
    struct imgqual qr, qc;
    float prev = 1e30f;

    sharp(ref);
    qr = meas(ref);
    CHECK(imgqual_judge(&qr, &qr) == IQ_OK, "reference against itself");
    CHECK(qr.contrast > 0.5f && qr.edge_frac > 0.1f, "sharp scene: contrast %.2f edges %.2f", qr.contrast,
          qr.edge_frac);

    /* blur: sharpness falls monotonically, and the verdict follows the 0.35 ratio exactly */
    int seen_ok = 0, seen_blur = 0;
    for (int r = 1; r <= 6; r++) {
        box(ref, cur, r);
        qc = meas(cur);
        float ratio = qc.tenengrad / qr.tenengrad;
        enum imgqual_verdict v = imgqual_judge(&qc, &qr);
        CHECK(qc.tenengrad < prev, "blur r=%d did not lower sharpness", r);
        CHECK(v == (ratio < 0.35f ? IQ_BLURRED : IQ_OK), "blur r=%d ratio %.3f verdict %d", r, ratio, v);
        seen_ok |= v == IQ_OK;
        seen_blur |= v == IQ_BLURRED;
        prev = qc.tenengrad;
    }
    CHECK(seen_ok && seen_blur, "blur series should cross the threshold (ok %d blurred %d)", seen_ok, seen_blur);
    box(ref, cur, 1);
    qc = meas(cur);
    CHECK(imgqual_judge(&qc, &qr) == IQ_OK, "3x3 blur (%.2f x) already BLURRED", qc.tenengrad / qr.tenengrad);
    box(ref, cur, 4);
    qc = meas(cur);
    CHECK(imgqual_judge(&qc, &qr) == IQ_BLURRED, "9x9 blur (%.2f x) not BLURRED", qc.tenengrad / qr.tenengrad);
    CHECK(imgqual_judge(&qc, NULL) == IQ_OK, "no reference: sharpness must not be judged");

    /* dark: too little signal to judge anything else, even when also blurred */
    remap(ref, cur, 0.12f, -100);
    qc = meas(cur);
    CHECK(imgqual_judge(&qc, &qr) == IQ_TOO_DARK, "dark frame p99 %u verdict %d", qc.p99, imgqual_judge(&qc, &qr));
    memset(cur, 2, sizeof(cur));      /* mostly black, a lit strip: dark_frac decides */
    memset(cur, 200, W * H / 4);
    qc = meas(cur);
    CHECK(qc.p99 >= 40 && qc.dark_frac > 0.6f && imgqual_judge(&qc, &qr) == IQ_TOO_DARK, "75 %% black frame");

    /* clipped */
    remap(ref, cur, 1.3f, 80);
    qc = meas(cur);
    CHECK(qc.sat_frac > 0.05f && imgqual_judge(&qc, &qr) == IQ_SATURATED, "clipped frame sat %.3f", qc.sat_frac);

    /* uniform contrast loss (fog) lowers the Sobel energy by k^2, so it is caught as BLURRED
     * before the contrast rule; either way the lens is reported */
    remap(ref, cur, 0.3f, 0);
    qc = meas(cur);
    CHECK(imgqual_judge(&qc, &qr) == IQ_BLURRED, "fogged frame verdict %d", imgqual_judge(&qc, &qr));

    /* contrast loss that keeps the edges: the reference also held a black and a white patch
     * (a window, a shadow), the current frame lost them */
    static uint8_t ref2[W * H];
    sharp(ref2);
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 60; x++) {
            ref2[(20 + y) * W + 20 + x] = 0;
            ref2[(160 + y) * W + 240 + x] = 255;
        }
    struct imgqual qr2 = meas(ref2);
    remap(ref, cur, 0.85f, 0);
    qc = meas(cur);
    CHECK(qc.tenengrad >= 0.35f * qr2.tenengrad && imgqual_judge(&qc, &qr2) == IQ_LOW_CONTRAST,
          "lost extremes: contrast %.2f / %.2f, sharpness %.2f x, verdict %d", qc.contrast, qr2.contrast,
          qc.tenengrad / qr2.tenengrad, imgqual_judge(&qc, &qr2));
}

/* the thresholds on the numbers themselves */
static void t_thresholds(void)
{
    struct imgqual r, c;

    memset(&r, 0, sizeof(r));
    r.p99 = 200;
    r.tenengrad = 1000;
    r.contrast = 0.6f;
    c = r;
    c.tenengrad = 350;
    CHECK(imgqual_judge(&c, &r) == IQ_OK, "exactly 0.35 x is not blurred");
    c.tenengrad = 349.9f;
    CHECK(imgqual_judge(&c, &r) == IQ_BLURRED, "just below 0.35 x");
    c = r;
    c.contrast = 0.3f;
    CHECK(imgqual_judge(&c, &r) == IQ_OK, "exactly half the contrast is OK");
    c.contrast = 0.299f;
    CHECK(imgqual_judge(&c, &r) == IQ_LOW_CONTRAST, "just below half the contrast");
    c = r;
    c.p99 = 39;
    CHECK(imgqual_judge(&c, &r) == IQ_TOO_DARK, "p99 39");
    c.p99 = 40;
    CHECK(imgqual_judge(&c, &r) == IQ_OK, "p99 40");
    c.sat_frac = 0.0501f;
    CHECK(imgqual_judge(&c, &r) == IQ_SATURATED, "5 %% clipped");
    r.tenengrad = 0;                  /* a reference without edges cannot judge sharpness */
    c = r;
    CHECK(imgqual_judge(&c, &r) == IQ_OK, "zero reference sharpness");

    /* LED step: down 30 % on clipping, up 40 % (+1) when dim, else hold */
    memset(&c, 0, sizeof(c));
    c.p99 = 255;
    c.sat_frac = 0.02f;
    CHECK(imgqual_led_step(&c, 100) == 70 && imgqual_led_step(&c, 6) == 4, "back-off");
    CHECK(imgqual_led_step(&c, 5) == 5, "back-off below 5 %%");
    c.sat_frac = 0.01f;
    CHECK(imgqual_led_step(&c, 100) == 100, "1 %% clipped is tolerated");
    c.p99 = 120;
    c.sat_frac = 0;
    CHECK(imgqual_led_step(&c, 50) == 71 && imgqual_led_step(&c, 80) == 100 && imgqual_led_step(&c, 100) == 100 &&
          imgqual_led_step(&c, 0) == 1, "raise when dim");
    c.sat_frac = 0.005f;              /* dim but a specular highlight clips: hold */
    CHECK(imgqual_led_step(&c, 50) == 50, "dim with a clipped highlight raised");
    c.p99 = 150;
    c.sat_frac = 0;
    CHECK(imgqual_led_step(&c, 50) == 50, "p99 150 raised");
}

/* LED loop on a lit scene: brightness proportional to the LED setting */
static unsigned run_led(float gain, unsigned start, unsigned *steps, int *monotone)
{
    static uint8_t refl[W * H], img[W * H];
    unsigned pct = start, prev = start;
    int dir = 0;

    sharp(refl);
    *monotone = 1;
    for (*steps = 0; *steps < 20; (*steps)++) {
        for (int i = 0; i < W * H; i++)
            img[i] = clampb(refl[i] * gain * pct / 100.0f);
        struct imgqual q = meas(img);
        unsigned nx = imgqual_led_step(&q, pct);
        if (nx == pct)
            return pct;
        int d = nx > pct ? 1 : -1;
        if (dir && d != dir)
            *monotone = 0;
        dir = d;
        prev = pct;
        pct = nx;
    }
    (void)prev;
    return 1000;    /* no fixed point: oscillates */
}

static void t_led_loop(void)
{
    unsigned steps, pct;
    int mono;
    static uint8_t img[W * H], refl[W * H];

    /* 2.5x overexposed at 100 %: backs off only, and settles unclipped */
    pct = run_led(2.5f, 100, &steps, &mono);
    CHECK(pct < 100 && mono && steps <= 6, "overexposed scene: %u %% after %u steps, monotone %d", pct, steps, mono);
    sharp(refl);
    for (int i = 0; i < W * H; i++)
        img[i] = clampb(refl[i] * 2.5f * pct / 100.0f);
    struct imgqual q = meas(img);
    CHECK(q.sat_frac <= 0.01f && imgqual_judge(&q, NULL) == IQ_OK, "settled frame sat %.3f", q.sat_frac);

    /* a far, dim scene: stays at maximum (2 m must be lit) */
    pct = run_led(0.5f, 100, &steps, &mono);
    CHECK(pct == 100 && steps == 0, "dim scene moved the LEDs to %u %%", pct);

    /* started low: raises only, stops once lit, never overshoots into clipping */
    pct = run_led(1.2f, 10, &steps, &mono);
    CHECK(pct > 10 && mono, "raise from 10 %%: %u %% monotone %d", pct, mono);
    for (int i = 0; i < W * H; i++)
        img[i] = clampb(refl[i] * 1.2f * pct / 100.0f);
    q = meas(img);
    CHECK(q.sat_frac <= 0.01f && (q.p99 >= 150 || pct == 100), "raised to %u %%: p99 %u sat %.3f", pct, q.p99,
          q.sat_frac);

    /* no scene gain in between makes the loop oscillate */
    for (float g = 0.3f; g <= 6.0f; g += 0.05f) {
        pct = run_led(g, 100, &steps, &mono);
        CHECK(pct <= 100 && mono, "gain %.2f: %s", g, pct > 100 ? "oscillates" : "changes direction");
    }
}

int main(void)
{
    t_numbers();
    t_verdicts();
    t_thresholds();
    t_led_loop();
    if (fails)
        printf("imgqual: FAIL (%d)\n", fails);
    else
        printf("imgqual: histogram, blur series across the 0.35x threshold, dark/clipped/low-contrast "
               "verdicts, LED step and loop all pass\n");
    return fails != 0;
}
