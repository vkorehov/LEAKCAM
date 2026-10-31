/*
 * test008: the wake path's CPU work without a camera, to compare compiler flags (the faster build stays).
 * Times each step leakcam_wake runs per wake on a synthetic 1280x960 frame pair (fixed seed, so every build
 * computes the same thing): imgdiff_reduce (both cameras), imgdiff_compare, the history keyframe compression
 * (compress2 at history.c's level) and the CRC. Not the change net: it runs on the KPU (the flags do not touch it)
 * and nncase would make each variant ~5 MB, too much for six of them in /sdcard. Built with the programs
 * (LEAKCAM_BENCH=1 in attic/firmware/k230_capture/rtsmart/Makefile), never part of the product image.
 *
 *   leakcam_cpubench [runs]    (default 20) -> min and median microseconds per step
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "imgdiff.h"
#include "refstore.h"
#include "miniz.h"

#define W 1280
#define H 960
#define ZLEVEL_HIST 3               /* history.c ZLEVEL */
#define MAX_RUNS 200

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

/* a textured scene: gradient + noise + a few shapes; the second frame moves a shape and changes the light */
static void scene(uint8_t *p, int shift, int gain, unsigned seed)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            seed = seed * 1103515245u + 12345u;
            int v = (x * 160 / W) + (y * 60 / H) + (int)((seed >> 16) & 15);
            if ((x - 400 - shift) * (x - 400 - shift) + (y - 480) * (y - 480) < 120 * 120)
                v += 60;
            if (x > 900 && x < 1100 && y > 200 && y < 500)
                v -= 50;
            v = v * gain / 100;
            p[y * W + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *name, uint64_t *t, int n)
{
    qsort(t, (size_t)n, sizeof(t[0]), cmp_u64);
    printf("bench %-16s min %8llu us  median %8llu us\n", name, (unsigned long long)t[0], (unsigned long long)t[n / 2]);
}

int main(int argc, char **argv)
{
    int runs = argc > 1 ? atoi(argv[1]) : 20;
    if (runs < 1 || runs > MAX_RUNS)
        runs = 20;
    static uint8_t a[W * H], b[W * H], ra[IMGDIFF_W * IMGDIFF_H], rb[IMGDIFF_W * IMGDIFF_H];
    static uint64_t t_reduce[MAX_RUNS], t_cmp[MAX_RUNS], t_z[MAX_RUNS], t_crc[MAX_RUNS];
    scene(a, 0, 100, 1);
    scene(b, 40, 115, 2);

    struct imgdiff_cfg cfg;
    imgdiff_default_cfg(&cfg);
    uLongf zcap = compressBound(W * H);
    uint8_t *z = malloc(zcap);
    struct imgdiff_result res;
    uLongf zlen = 0;
    uint32_t crc = 0;

    for (int i = 0; i < runs; i++) {
        uint64_t t0 = now_us();
        imgdiff_reduce(a, W, H, W, ra);
        imgdiff_reduce(b, W, H, W, rb);
        uint64_t t1 = now_us();
        imgdiff_compare(ra, rb, &cfg, &res);
        uint64_t t3 = now_us();
        zlen = zcap;
        compress2(z, &zlen, b, W * H, ZLEVEL_HIST);
        uint64_t t4 = now_us();
        crc = refstore_crc32(b, W * H);
        uint64_t t5 = now_us();
        t_reduce[i] = t1 - t0;
        t_cmp[i] = t3 - t1;
        t_z[i] = t4 - t3;
        t_crc[i] = t5 - t4;
    }
    /* the results, so every build can be checked to compute the same */
    printf("bench check: changed %d blocks %d mad %.3f z %lu crc %08lx\n", res.changed, res.changed_blocks,
           res.max_block_mad, (unsigned long)zlen, (unsigned long)crc);
    report("reduce x2", t_reduce, runs);
    report("compare", t_cmp, runs);
    report("compress key", t_z, runs);
    report("crc32", t_crc, runs);
    free(z);
    return 0;
}
