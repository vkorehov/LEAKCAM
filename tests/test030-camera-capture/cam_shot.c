/*
 * test030: one picture per camera, for the bench. Captures through vicap_cap.c (the wake path's capture,
 * 1280x960 binned, offline mode), keeps the whole NV12 frame (Y plane + interleaved UV plane) and
 *   - writes it raw to /sdcard/cam<slot>.nv12 (width x height x 3 / 2 bytes)
 *   - prints it zlib-compressed in base64 on the console between "CAMSHOT <slot> <w> <h> <zlen> nv12" and
 *     "CAMSHOT END", for tests/test030-camera-capture/shot.py to turn into a colour PNG on the PC.
 *
 *   cam_shot [cams] [settle]    cams: "0" (CSI0, J4), "1" (CSI2, J5) or "01"; settle: frames dropped first (AE)
 *
 * Test-only (built with LEAKCAM_BENCH=1 by tests/firmware/test_install.sh), never in the product image.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cap.h"
#include "miniz.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void print_b64(const unsigned char *p, size_t n)
{
    char line[77];
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        line[k++] = B64[v >> 18 & 63];
        line[k++] = B64[v >> 12 & 63];
        line[k++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
        line[k++] = i + 2 < n ? B64[v & 63] : '=';
        if (k == 76) {
            line[k] = 0;
            puts(line);
            k = 0;
        }
    }
    if (k) {
        line[k] = 0;
        puts(line);
    }
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "0";
    unsigned settle = argc > 2 ? (unsigned)atoi(argv[2]) : 20;
    struct cap_cam cams[CAP_MAX_CAMS];
    int n = 0;
    memset(cams, 0, sizeof(cams));
    for (const char *c = which; *c && n < 2; c++)
        if (*c == '0' || *c == '1')
            cams[n++].node = (unsigned)(*c - '0');
    if (!n) {
        fprintf(stderr, "usage: cam_shot [0|1|01] [settle]\n");
        return 2;
    }
    double t0 = now_s();
    if (cap_open_all(cams, n, 1280, 960) < 0) {
        cap_close_all(cams, n);
        return 1;
    }
    for (int i = 0; i < n; i++)
        cams[i].chroma = malloc((size_t)cams[i].width * cams[i].height / 2);
    double t1 = now_s();
    if (cap_grab_all(cams, n, settle, 1000) < 0) {
        cap_close_all(cams, n);
        return 1;
    }
    double t2 = now_s();
    printf("cam_shot: open %.2f s, %u settle frames + 1 in %.2f s\n", t1 - t0, settle, t2 - t1);
    int rc = 0;
    for (int i = 0; i < n; i++) {
        struct cap_cam *c = &cams[i];
        size_t len = (size_t)c->width * c->height, uvlen = c->chroma ? len / 2 : 0;
        unsigned char *frame = malloc(len + uvlen);
        if (!frame) {
            rc = 1;
            continue;
        }
        memcpy(frame, c->luma, len);
        if (uvlen)
            memcpy(frame + len, c->chroma, uvlen);
        unsigned long lo = 255, hi = 0, sum = 0;
        for (size_t j = 0; j < len; j++) {
            unsigned v = c->luma[j];
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
            sum += v;
        }
        printf("cam_shot: cam %u %ux%u, luma min %lu mean %lu max %lu\n", c->slot, c->width, c->height, lo, hi ? sum / len : 0, hi);
        char path[40];
        snprintf(path, sizeof(path), uvlen ? "/sdcard/cam%u.nv12" : "/sdcard/cam%u.y", c->slot);
        FILE *fp = fopen(path, "wb");
        if (!fp || fwrite(frame, 1, len + uvlen, fp) != len + uvlen) {
            fprintf(stderr, "cam_shot: %s not written\n", path);
            rc = 1;
        }
        if (fp)
            fclose(fp);
        mz_ulong zlen = compressBound((mz_ulong)(len + uvlen));
        unsigned char *z = malloc(zlen);
        if (!z || compress2(z, &zlen, frame, (mz_ulong)(len + uvlen), 6) != Z_OK) {
            fprintf(stderr, "cam_shot: compress failed\n");
            free(z);
            free(frame);
            rc = 1;
            continue;
        }
        free(frame);
        printf("CAMSHOT %u %u %u %lu %s\n", c->slot, c->width, c->height, (unsigned long)zlen, uvlen ? "nv12" : "y");
        print_b64(z, zlen);
        printf("CAMSHOT END\n");
        fflush(stdout);
        free(z);
    }
    for (int i = 0; i < n; i++) {
        free(cams[i].chroma);
        cams[i].chroma = NULL;
    }
    cap_close_all(cams, n);
    return rc;
}
