/*
 * fdump: a file from the board to the PC over the console (no network on the bench image): zlib, then base64
 * between "FDUMP <size> <zlen>" and "FDUMP END"; tests/test030-camera-capture/stream.py decodes it.
 *
 *   fdump <file>
 *
 * Test-only (built with LEAKCAM_BENCH=1 by tests/firmware/test_install.sh), never in the product image.
 */
#include <stdio.h>
#include <stdlib.h>

#include "miniz.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: fdump <file>\n");
        return 2;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "fdump: %s: cannot open\n", argv[1]);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *buf = malloc(n > 0 ? (size_t)n : 1);
    mz_ulong zlen = compressBound((mz_ulong)n);
    unsigned char *z = malloc(zlen);
    if (!buf || !z || fread(buf, 1, (size_t)n, fp) != (size_t)n ||
        compress2(z, &zlen, buf, (mz_ulong)n, 6) != Z_OK) {
        fprintf(stderr, "fdump: %s: read or compress failed\n", argv[1]);
        fclose(fp);
        return 1;
    }
    fclose(fp);
    free(buf);
    printf("FDUMP %ld %lu\n", n, (unsigned long)zlen);
    char line[77];
    size_t k = 0;
    for (size_t i = 0; i < zlen; i += 3) {
        unsigned v = (unsigned)z[i] << 16 | (i + 1 < zlen ? (unsigned)z[i + 1] << 8 : 0) | (i + 2 < zlen ? z[i + 2] : 0);
        line[k++] = B64[v >> 18 & 63];
        line[k++] = B64[v >> 12 & 63];
        line[k++] = i + 1 < zlen ? B64[v >> 6 & 63] : '=';
        line[k++] = i + 2 < zlen ? B64[v & 63] : '=';
        if (k == 76 || i + 3 >= zlen) {
            line[k] = 0;
            puts(line);
            k = 0;
        }
    }
    printf("FDUMP END\n");
    free(z);
    return 0;
}
