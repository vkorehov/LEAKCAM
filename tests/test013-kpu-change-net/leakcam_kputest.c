/*
 * test013: the change net on the KPU without a camera (2026-10-25, replaces the lost SDK-copy leakcam_kputest).
 * Loads the factory kmodel as leakcam_wake does (change_load), then times change_distance() on synthetic
 * IMGDIFF_W x IMGDIFF_H luma pairs with a fixed seed: an identical pair (distance must be ~0), the same scene a
 * quarter brighter (light: below CHANGE_THRESHOLD expected) and a new scene (change: above it expected). Built with the
 * programs only with LEAKCAM_BENCH=1 (firmware/k230_capture/rtsmart/Makefile), never part of the product image.
 *
 *   leakcam_kputest [runs]    (default 20) -> load time, per-pair min / median / max ms, the three distances
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "change.h"
#include "imgdiff.h"

#define MAX_RUNS 200

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* a scene: smooth shapes plus fixed noise, so the net sees structure (pure noise or flat frames tell nothing) */
static void scene(uint8_t *f, unsigned seed, int gain_pct)
{
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            seed = seed * 1103515245u + 12345u;
            int v = 60 + ((x / 40 + y / 30) % 2) * 70 + (int)((seed >> 16) & 15);
            v = v * gain_pct / 100;
            f[y * IMGDIFF_W + x] = (uint8_t)(v > 255 ? 255 : v);
        }
}

int main(int argc, char **argv)
{
    int runs = argc > 1 ? atoi(argv[1]) : 20;
    if (runs < 1 || runs > MAX_RUNS)
        runs = 20;
    static uint8_t a[IMGDIFF_W * IMGDIFF_H], light[IMGDIFF_W * IMGDIFF_H], other[IMGDIFF_W * IMGDIFF_H];
    static uint64_t t[MAX_RUNS];
    FILE *fp = fopen(CHANGE_KMODEL, "rb");
    if (!fp) {
        printf("kputest: cannot open %s\n", CHANGE_KMODEL);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *km = malloc((size_t)len);
    if (!km || fread(km, 1, (size_t)len, fp) != (size_t)len) {
        printf("kputest: cannot read %s\n", CHANGE_KMODEL);
        return 1;
    }
    fclose(fp);
    uint64_t t0 = now_us();
    if (change_load(km, (size_t)len) < 0)
        return 1;
    printf("kputest: model %ld B loaded in %llu us\n", len, (unsigned long long)(now_us() - t0));
    scene(a, 1, 100);
    scene(light, 1, 125);
    scene(other, 7, 100);
    float same = change_distance(a, a);           /* the first inference also warms the runtime up */
    for (int i = 0; i < runs; i++) {
        t0 = now_us();
        same = change_distance(a, a);
        t[i] = now_us() - t0;
    }
    qsort(t, (size_t)runs, sizeof(t[0]), cmp_u64);
    float dl = change_distance(a, light), dc = change_distance(a, other);
    printf("kputest: pair %d runs: min %llu / median %llu / max %llu us\n", runs, (unsigned long long)t[0],
           (unsigned long long)t[runs / 2], (unsigned long long)t[runs - 1]);
    printf("kputest: distance same %.4f, light %.4f, change %.4f (threshold %.2f)\n", same, dl, dc, CHANGE_THRESHOLD);
    printf("kputest: %s\n", same >= 0 && same < 0.01f && dl >= 0 && dc > dl ? "PASS" : "FAIL");
    free(km);
    return 0;
}
