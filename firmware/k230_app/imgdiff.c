#include "imgdiff.h"

#include <math.h>
#include <string.h>

void imgdiff_default_cfg(struct imgdiff_cfg *cfg)
{
    /* 222-degree lens on a 1/4" sensor: the image circle is wider than the frame height, so the
     * mask mostly removes the dark corners */
    cfg->circle_radius = 0.62f;
    cfg->pixel_thresh = 30;     /* native, measured 2026-11-12 (test144): an unchanged LED pair at most 17 pixels over
                                 * it per block, a 10 cm puddle at 3 m 78 */
    cfg->block_thresh = 12.0f;
    cfg->block_pixels = 24;
    cfg->frac_thresh = 0.01f;
    cfg->dark_floor = 4;
}

/* each row's span [x0, x1) inside the image circle: no per-pixel mask test over 5 M pixels */
static void spans(const struct imgdiff_cfg *cfg, int16_t x0[IMGDIFF_H], int16_t x1[IMGDIFF_H])
{
    float r = cfg->circle_radius * IMGDIFF_W;
    for (int y = 0; y < IMGDIFF_H; y++) {
        float dy = y + 0.5f - IMGDIFF_H / 2.0f, h2 = r * r - dy * dy;
        float hw = cfg->circle_radius <= 0 ? IMGDIFF_W : (h2 > 0 ? sqrtf(h2) : -1.0f);
        int a = hw < 0 ? 0 : (int)ceilf(IMGDIFF_W / 2.0f - hw - 0.5f);
        int b = hw < 0 ? 0 : (int)floorf(IMGDIFF_W / 2.0f + hw - 0.5f) + 1;
        x0[y] = (int16_t)(a < 0 ? 0 : a);
        x1[y] = (int16_t)(b > IMGDIFF_W ? IMGDIFF_W : b);
    }
}

void imgdiff_compare(const uint8_t *ref, const uint8_t *cur, int cur_stride, const struct imgdiff_cfg *cfg,
                     struct imgdiff_result *out)
{
    static int16_t x0[IMGDIFF_H], x1[IMGDIFF_H];
    static uint32_t bsum[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X], bcnt[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X];
    static uint16_t bpx[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X];
    const int dark = cfg->dark_floor;
    memset(out, 0, sizeof(*out));
    spans(cfg, x0, x1);

    /* 1. means over the usable area, for gain normalisation */
    uint64_t sr = 0, sc = 0, n = 0;
    for (int y = 0; y < IMGDIFF_H; y++) {
        const uint8_t *a = ref + (size_t)y * IMGDIFF_W, *b = cur + (size_t)y * cur_stride;
        for (int x = x0[y]; x < x1[y]; x++)
            if (a[x] >= dark || b[x] >= dark) {
                sr += a[x];
                sc += b[x];
                n++;
            }
    }
    if (n == 0)
        return;
    out->mean_ref = (float)sr / (float)n;
    out->mean_new = (float)sc / (float)n;
    float gain = out->mean_new > 1.0f ? out->mean_ref / out->mean_new : 1.0f;
    if (gain < 0.5f) gain = 0.5f;           /* beyond 2x it is a lighting failure, not exposure */
    if (gain > 2.0f) gain = 2.0f;
    out->gain = gain;

    /* 2. per-pixel and per-block differences (gain in Q8) */
    const int g = (int)lrintf(gain * 256.0f), pt = cfg->pixel_thresh * 256;
    memset(bsum, 0, sizeof(bsum));
    memset(bcnt, 0, sizeof(bcnt));
    memset(bpx, 0, sizeof(bpx));
    uint64_t changed_px = 0;
    for (int y = 0; y < IMGDIFF_H; y++) {
        const uint8_t *a = ref + (size_t)y * IMGDIFF_W, *b = cur + (size_t)y * cur_stride;
        uint32_t *s = bsum[y / IMGDIFF_BLOCK], *c = bcnt[y / IMGDIFF_BLOCK];
        uint16_t *p = bpx[y / IMGDIFF_BLOCK];
        for (int x = x0[y]; x < x1[y]; x++) {
            if (a[x] < dark && b[x] < dark)
                continue;
            int d = a[x] * 256 - g * b[x];
            if (d < 0)
                d = -d;
            changed_px += d > pt;
            p[x / IMGDIFF_BLOCK] += d > pt;
            s[x / IMGDIFF_BLOCK] += (uint32_t)d;
            c[x / IMGDIFF_BLOCK]++;
        }
    }
    for (int by = 0; by < IMGDIFF_BLOCKS_Y; by++)
        for (int bx = 0; bx < IMGDIFF_BLOCKS_X; bx++) {
            /* blocks less than a quarter inside the circle are too noisy to judge */
            if (bcnt[by][bx] < IMGDIFF_BLOCK * IMGDIFF_BLOCK / 4)
                continue;
            out->usable_blocks++;
            float mad = (float)bsum[by][bx] / (256.0f * (float)bcnt[by][bx]);
            if (mad > out->max_block_mad)
                out->max_block_mad = mad;
            if (mad > cfg->block_thresh || bpx[by][bx] >= cfg->block_pixels) {
                out->changed_blocks++;
                out->block_changed[by][bx] = 1;
            }
        }
    out->changed_frac = (float)changed_px / (float)n;
    out->changed = out->changed_blocks > 0 || out->changed_frac > cfg->frac_thresh;
}
