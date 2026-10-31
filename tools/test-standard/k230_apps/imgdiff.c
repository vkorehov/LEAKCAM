#include "imgdiff.h"

#include <math.h>
#include <string.h>

#define N (IMGDIFF_W * IMGDIFF_H)

void imgdiff_default_cfg(struct imgdiff_cfg *cfg)
{
    /* 222-degree lens on a 1/4" sensor: the image circle is wider than the frame height, so the
     * mask mostly removes the dark corners */
    cfg->circle_radius = 0.62f;
    cfg->pixel_thresh = 25;
    cfg->block_thresh = 12.0f;
    cfg->frac_thresh = 0.01f;
    cfg->dark_floor = 4;
}

int imgdiff_reduce(const uint8_t *src, int w, int h, int stride, uint8_t *dst)
{
    if (w % IMGDIFF_W || h % IMGDIFF_H)
        return -1;
    int fx = w / IMGDIFF_W, fy = h / IMGDIFF_H, area = fx * fy;
    uint8_t tmp[N];
    for (int y = 0; y < IMGDIFF_H; y++) {
        for (int x = 0; x < IMGDIFF_W; x++) {
            unsigned sum = 0;
            for (int j = 0; j < fy; j++) {
                const uint8_t *row = src + (size_t)(y * fy + j) * stride + x * fx;
                for (int i = 0; i < fx; i++)
                    sum += row[i];
            }
            tmp[y * IMGDIFF_W + x] = (uint8_t)((sum + area / 2) / area);
        }
    }
    /* 3x3 box blur, edges clamped: the per-pixel threshold then ignores single-pixel noise */
    for (int y = 0; y < IMGDIFF_H; y++) {
        for (int x = 0; x < IMGDIFF_W; x++) {
            unsigned sum = 0;
            for (int j = -1; j <= 1; j++) {
                int yy = y + j < 0 ? 0 : (y + j >= IMGDIFF_H ? IMGDIFF_H - 1 : y + j);
                for (int i = -1; i <= 1; i++) {
                    int xx = x + i < 0 ? 0 : (x + i >= IMGDIFF_W ? IMGDIFF_W - 1 : x + i);
                    sum += tmp[yy * IMGDIFF_W + xx];
                }
            }
            dst[y * IMGDIFF_W + x] = (uint8_t)((sum + 4) / 9);
        }
    }
    return 0;
}

static bool in_mask(int x, int y, const struct imgdiff_cfg *cfg)
{
    if (cfg->circle_radius <= 0)
        return true;
    float dx = x + 0.5f - IMGDIFF_W / 2.0f, dy = y + 0.5f - IMGDIFF_H / 2.0f;
    float r = cfg->circle_radius * IMGDIFF_W;
    return dx * dx + dy * dy <= r * r;
}

void imgdiff_compare(const uint8_t *ref, const uint8_t *cur, const struct imgdiff_cfg *cfg,
                     struct imgdiff_result *out)
{
    memset(out, 0, sizeof(*out));

    /* 1. means over the usable area, for gain normalisation */
    double sr = 0, sc = 0;
    unsigned n = 0;
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            int i = y * IMGDIFF_W + x;
            if (!in_mask(x, y, cfg) || (ref[i] < cfg->dark_floor && cur[i] < cfg->dark_floor))
                continue;
            sr += ref[i];
            sc += cur[i];
            n++;
        }
    if (n == 0)
        return;
    out->mean_ref = (float)(sr / n);
    out->mean_new = (float)(sc / n);
    float gain = out->mean_new > 1.0f ? out->mean_ref / out->mean_new : 1.0f;
    if (gain < 0.5f) gain = 0.5f;           /* beyond 2x it is a lighting failure, not exposure */
    if (gain > 2.0f) gain = 2.0f;
    out->gain = gain;

    /* 2. per-pixel and per-block differences */
    enum { BW = IMGDIFF_W / IMGDIFF_BLOCKS_X, BH = IMGDIFF_H / IMGDIFF_BLOCKS_Y };
    double bsum[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X] = { { 0 } };
    unsigned bcnt[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X] = { { 0 } };
    unsigned changed_px = 0;
    for (int y = 0; y < IMGDIFF_H; y++)
        for (int x = 0; x < IMGDIFF_W; x++) {
            int i = y * IMGDIFF_W + x;
            if (!in_mask(x, y, cfg) || (ref[i] < cfg->dark_floor && cur[i] < cfg->dark_floor))
                continue;
            float d = fabsf((float)ref[i] - gain * (float)cur[i]);
            if (d > cfg->pixel_thresh)
                changed_px++;
            bsum[y / BH][x / BW] += d;
            bcnt[y / BH][x / BW]++;
        }
    for (int by = 0; by < IMGDIFF_BLOCKS_Y; by++)
        for (int bx = 0; bx < IMGDIFF_BLOCKS_X; bx++) {
            /* blocks less than a quarter inside the circle are too noisy to judge */
            if (bcnt[by][bx] < (unsigned)(BW * BH / 4))
                continue;
            out->usable_blocks++;
            float mad = (float)(bsum[by][bx] / bcnt[by][bx]);
            if (mad > out->max_block_mad)
                out->max_block_mad = mad;
            if (mad > cfg->block_thresh) {
                out->changed_blocks++;
                out->block_changed[by][bx] = 1;
            }
        }
    out->changed_frac = (float)changed_px / (float)n;
    out->changed = out->changed_blocks > 0 || out->changed_frac > cfg->frac_thresh;
}
