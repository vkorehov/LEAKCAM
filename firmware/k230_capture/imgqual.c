#include "imgqual.h"

#include <string.h>

/* Sobel magnitude^2 below this is sensor noise (OV5647 at high gain, after the 4x4 box + 3x3
 * blur of imgdiff_reduce): ~ (2 * 4 * 3 levels)^2. Not yet measured on LEAKCAM frames. */
#define NOISE_G2 576

static int inside(int x, int y, int w, int h, float r)
{
    if (r <= 0)
        return 1;
    float dx = x + 0.5f - w / 2.0f, dy = y + 0.5f - h / 2.0f, rr = r * w;
    return dx * dx + dy * dy <= rr * rr;
}

static uint8_t pct(const unsigned hist[256], unsigned n, float p)
{
    unsigned want = (unsigned)(p * n), acc = 0;
    for (int v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc > want)
            return (uint8_t)v;
    }
    return 255;
}

void imgqual_measure(const uint8_t *img, int w, int h, float circle_radius, struct imgqual *q)
{
    unsigned hist[256];
    memset(hist, 0, sizeof hist);
    memset(q, 0, sizeof *q);
    double sum = 0, g2sum = 0, lsum = 0, l2sum = 0;
    unsigned n = 0, ng = 0, nl = 0, nedge = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!inside(x, y, w, h, circle_radius))
                continue;
            uint8_t v = img[y * w + x];
            hist[v]++;
            sum += v;
            n++;
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
                continue;
            const uint8_t *p = img + y * w + x;
            int gx = (p[-w + 1] + 2 * p[1] + p[w + 1]) - (p[-w - 1] + 2 * p[-1] + p[w - 1]);
            int gy = (p[w - 1] + 2 * p[w] + p[w + 1]) - (p[-w - 1] + 2 * p[-w] + p[-w + 1]);
            int g2 = gx * gx + gy * gy;
            if (g2 > NOISE_G2) {
                g2sum += g2;
                nedge++;
            }
            ng++;
            int lap = p[-1] + p[1] + p[-w] + p[w] - 4 * p[0];
            lsum += lap;
            l2sum += (double)lap * lap;
            nl++;
        }
    if (!n)
        return;
    q->mean = (float)(sum / n);
    q->p01 = pct(hist, n, 0.01f);
    q->p50 = pct(hist, n, 0.50f);
    q->p99 = pct(hist, n, 0.99f);
    unsigned sat = 0, dark = 0;
    for (int v = 250; v < 256; v++)
        sat += hist[v];
    for (int v = 0; v <= 8; v++)
        dark += hist[v];
    q->sat_frac = (float)sat / n;
    q->dark_frac = (float)dark / n;
    q->contrast = (q->p99 - q->p01) / 255.0f;
    q->edge_frac = ng ? (float)nedge / ng : 0;
    /* normalised by ALL pixels, not just the edge pixels: fewer edges = less sharp */
    q->tenengrad = ng ? (float)(g2sum / ng) : 0;
    if (nl) {
        double m = lsum / nl;
        q->lapvar = (float)(l2sum / nl - m * m);
    }
}

unsigned imgqual_led_step(const struct imgqual *q, unsigned cur)
{
    if (q->sat_frac > 0.01f && cur > 5)                 /* clipping: back off 30 % */
        return cur * 7 / 10;
    if (q->p99 < 150 && q->sat_frac < 0.001f && cur < 100) {   /* dim and nothing clips */
        unsigned nx = cur * 14 / 10 + 1;
        return nx > 100 ? 100 : nx;
    }
    return cur;
}

enum imgqual_verdict imgqual_judge(const struct imgqual *cur, const struct imgqual *ref)
{
    if (cur->p99 < 40 || cur->dark_frac > 0.6f)
        return IQ_TOO_DARK;
    if (cur->sat_frac > 0.05f)
        return IQ_SATURATED;
    /* thresholds are starting points measured on leakcam's procedural cohorts
     * (nn_poc/README.md, "Image quality"); re-measure on LEAKCAM frames */
    if (ref && ref->tenengrad > 0 && cur->tenengrad < 0.35f * ref->tenengrad)
        return IQ_BLURRED;
    if (ref && ref->contrast > 0 && cur->contrast < 0.5f * ref->contrast)
        return IQ_LOW_CONTRAST;
    return IQ_OK;
}
