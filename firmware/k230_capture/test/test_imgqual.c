/* host test: imgqual on leakcam PGM pairs. Usage: test_imgqual list.txt (lines: kind ref.pgm cur.pgm) */
#include <stdio.h>
#include <stdlib.h>
#include "imgdiff.h"
#include "imgqual.h"

static int readpgm(const char *p, uint8_t *buf, int *w, int *h)
{
    FILE *f = fopen(p, "rb");
    int mx;
    if (!f || fscanf(f, "P5 %d %d %d", w, h, &mx) != 3) { fprintf(stderr, "bad %s\n", p); exit(1); }
    fgetc(f);
    size_t n = fread(buf, 1, (size_t)*w * *h, f);
    fclose(f);
    return n == (size_t)*w * *h ? 0 : -1;
}

int main(int argc, char **argv)
{
    static uint8_t a[1280 * 960], b[1280 * 960], ra[IMGDIFF_W * IMGDIFF_H], rb[IMGDIFF_W * IMGDIFF_H];
    char kind[64], pr[512], pc[512];
    FILE *l = argc == 2 ? fopen(argv[1], "r") : NULL;
    if (!l) { fprintf(stderr, "usage: test_imgqual list.txt (lines: kind ref.pgm cur.pgm)\n"); return 1; }
    while (fscanf(l, "%63s %511s %511s", kind, pr, pc) == 3) {
        int w, h;
        readpgm(pr, a, &w, &h);
        imgdiff_reduce(a, w, h, w, ra);
        readpgm(pc, b, &w, &h);
        imgdiff_reduce(b, w, h, w, rb);
        struct imgqual qr, qc;
        imgqual_measure(ra, IMGDIFF_W, IMGDIFF_H, 0, &qr);
        imgqual_measure(rb, IMGDIFF_W, IMGDIFF_H, 0, &qc);
        printf("%s %.4f %.4f %.4f %d %d %.4f %.4f %u\n", kind, qc.tenengrad / (qr.tenengrad + 1e-6f),
               qc.lapvar / (qr.lapvar + 1e-6f), qc.contrast / (qr.contrast + 1e-6f), qc.p99, qc.p01,
               qc.sat_frac, qc.mean, imgqual_led_step(&qc, 100));
    }
    return 0;
}
