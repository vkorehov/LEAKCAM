/*
 * TEMPORARY (LEAKCAM tests/test024 DDR stress), SDK copy only: copied to src/applications/memtest and listed in
 * apps.mk by hand. DELETE from the SDK after the test.
 *
 *   memtest [passes] [reserve_mb]
 *
 * Takes all it can of both DDR regions in 1 MB blocks and checks them, pass after pass:
 *   - RT-Smart system RAM (58 MB) through malloc, cached; reserve_mb (default 4) handed back for the system;
 *   - MMZ, the media pool (70 MB), through kd_mpi_sys_mmz_alloc, uncached: every access goes to the DDR.
 * Patterns per pass: walking one (bit = pass % 64), its inverse, address-in-address, inverse address, and a
 * xorshift random stream (seed = pass). Every mismatch is printed with region, address, pattern, wrote/read;
 * a line per pass with MB, time, MB/s and the error total. passes 0 = until reset.
 * Sleeps 1 ms per block: the 20 s watchdog's feeder must run (skill k230-rtsmart-hangs).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "mpi_sys_api.h"

#define BLOCK      (1u << 20)
#define MAX_BLOCKS 160
#define MAX_PRINT  64                   /* mismatch lines per pass, the count goes on */

struct block {
    uint64_t *p;
    uint64_t phys;                       /* MMZ only */
    int mmz;
};

static struct block blk[MAX_BLOCKS];
static int nblk;
static unsigned long errors, printed;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint64_t pattern(int kind, unsigned pass, uint64_t addr, uint64_t *rnd)
{
    switch (kind) {
    case 0: return 1ull << (pass % 64);
    case 1: return ~(1ull << (pass % 64));
    case 2: return addr;
    case 3: return ~addr;
    default:                             /* xorshift64 */
        *rnd ^= *rnd << 13, *rnd ^= *rnd >> 7, *rnd ^= *rnd << 17;
        return *rnd;
    }
}

static const char *const kind_name[] = { "walk1", "walk0", "addr", "~addr", "random" };

static void run_pattern(int kind, unsigned pass)
{
    uint64_t rnd = 0x9E3779B97F4A7C15ull ^ pass;
    for (int b = 0; b < nblk; b++) {
        for (size_t i = 0; i < BLOCK / 8; i++)
            blk[b].p[i] = pattern(kind, pass, (uint64_t)(uintptr_t)&blk[b].p[i], &rnd);
        usleep(1000);
    }
    rnd = 0x9E3779B97F4A7C15ull ^ pass;
    for (int b = 0; b < nblk; b++) {
        for (size_t i = 0; i < BLOCK / 8; i++) {
            uint64_t want = pattern(kind, pass, (uint64_t)(uintptr_t)&blk[b].p[i], &rnd), got = blk[b].p[i];
            if (got != want) {
                errors++;
                if (printed++ < MAX_PRINT)
                    printf("ERROR pass %u %s %s %p (phys 0x%llx): wrote %016llx read %016llx\n", pass,
                           kind_name[kind], blk[b].mmz ? "mmz" : "sys", (void *)&blk[b].p[i],
                           blk[b].mmz ? (unsigned long long)(blk[b].phys + i * 8) : 0ull,
                           (unsigned long long)want, (unsigned long long)got);
            }
        }
        usleep(1000);
    }
}

int main(int argc, char **argv)
{
    unsigned passes = argc > 1 ? (unsigned)atoi(argv[1]) : 10;
    int reserve = argc > 2 ? atoi(argv[2]) : 4, nsys = 0, nmmz = 0;

    while (nblk < MAX_BLOCKS && (blk[nblk].p = malloc(BLOCK)) != NULL)
        nblk++, nsys++;
    for (int i = 0; i < reserve && nsys > 0; i++)
        free(blk[--nblk].p), nsys--;
    while (nblk < MAX_BLOCKS &&
           kd_mpi_sys_mmz_alloc(&blk[nblk].phys, (void **)&blk[nblk].p, "memtest", "anonymous", BLOCK) == 0)
        blk[nblk++].mmz = 1, nmmz++;
    printf("memtest: %d MB system RAM (cached, %d MB left free) + %d MB MMZ (uncached), %u passes\n", nsys,
           reserve, nmmz, passes);
    fflush(stdout);

    double t0 = now_s();
    for (unsigned pass = 0; passes == 0 || pass < passes; pass++) {
        double t = now_s();
        unsigned long before = errors;
        printed = 0;
        for (int kind = 0; kind < 5; kind++)
            run_pattern(kind, pass);
        double dt = now_s() - t;
        printf("pass %u: %d MB x 5 patterns in %.1f s (%.0f MB/s write+verify), errors %lu (total %lu), %.0f s\n",
               pass, nblk, dt, nblk * 10 / dt, errors - before, errors, now_s() - t0);
        fflush(stdout);
    }
    for (int b = 0; b < nblk; b++) {
        if (blk[b].mmz)
            kd_mpi_sys_mmz_free(blk[b].phys, blk[b].p);
        else
            free(blk[b].p);
    }
    printf("memtest: done, %lu errors\n", errors);
    return errors != 0;
}
