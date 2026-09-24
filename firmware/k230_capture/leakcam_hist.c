/*
 * leakcam_hist: read the image history leakcam_capture keeps on the NAND.
 *
 *   leakcam_hist [-d /var/lib/leakcam] list <cam>
 *   leakcam_hist [-d /var/lib/leakcam] get <cam> <seq|0> <out.pgm>     0 = newest
 *
 * list prints one line per record (sequence, kind, keyframe it builds on, time, blocks, bytes);
 * get rebuilds the full-resolution frame at that sequence from its keyframe and deltas.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "history.h"

static int usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [-d dir] list <cam>\n"
                    "       %s [-d dir] get <cam> <seq|0> <out.pgm>\n", argv0, argv0);
    return 2;
}

int main(int argc, char **argv)
{
    const char *dir = "/var/lib/leakcam";
    int opt;
    while ((opt = getopt(argc, argv, "d:")) != -1) {
        if (opt != 'd')
            return usage(argv[0]);
        dir = optarg;
    }
    if (argc - optind < 2)
        return usage(argv[0]);
    const char *cmd = argv[optind];
    int cam = atoi(argv[optind + 1]);

    if (!strcmp(cmd, "list")) {
        struct hist_entry *e;
        size_t n;
        if (hist_list(dir, cam, &e, &n) < 0) {
            perror("hist_list");
            return 1;
        }
        size_t total = 0;
        for (size_t i = 0; i < n; i++) {
            char when[32];
            struct tm tm;
            gmtime_r(&e[i].taken, &tm);
            strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", &tm);
            printf("%8u %s key %8u %s %4ux%-4u blocks %3u bytes %zu\n", e[i].seq,
                   e[i].kind == HIST_KEY ? "K" : "D", e[i].key_seq, when, e[i].w, e[i].h,
                   e[i].nblocks, e[i].bytes);
            total += e[i].bytes;
        }
        printf("cam %d: %zu records, %zu bytes\n", cam, n, total);
        free(e);
        return 0;
    }
    if (!strcmp(cmd, "get") && argc - optind == 4) {
        uint8_t *luma;
        unsigned w, h;
        time_t t;
        uint32_t seq = (uint32_t)strtoul(argv[optind + 2], NULL, 10);
        int rc = hist_rebuild(dir, cam, seq, &luma, &w, &h, &t);
        if (rc) {
            fprintf(stderr, rc > 0 ? "cam %d seq %u: not stored, or its keyframe is missing or "
                                     "damaged\n" : "cam %d seq %u: read error\n", cam, seq);
            return 1;
        }
        rc = hist_write_pgm(argv[optind + 3], luma, w, h);
        free(luma);
        if (rc < 0) {
            perror(argv[optind + 3]);
            return 1;
        }
        return 0;
    }
    return usage(argv[0]);
}
