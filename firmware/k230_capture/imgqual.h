/*
 * Cheap image-quality signals on the reduced luma frame (IMGDIFF_W x IMGDIFF_H, the output of
 * imgdiff_reduce()), for LED brightness control and for defocus / fog / dirt detection.
 * Pure C, no allocation, one pass for the histogram plus one pass for the gradients: about
 * 0.3 M operations at 320x240, negligible next to the capture itself.
 *
 * Nothing here drives focus (the M12 lenses are fixed-focus). Sharpness is only meaningful
 * RELATIVE to the same camera's reference frame under similar light: a fogged, dirty or
 * knocked lens lowers it against the reference, a dark scene lowers it too (less signal), which
 * is why the ratio is only judged when the exposure check passes.
 */
#ifndef LEAKCAM_IMGQUAL_H
#define LEAKCAM_IMGQUAL_H

#include <stdint.h>

struct imgqual {
    float mean;         /* mean luma inside the image circle */
    uint8_t p01, p50, p99;
    float sat_frac;     /* fraction of pixels >= 250: LED too bright / specular */
    float dark_frac;    /* fraction of pixels <= 8: LED too weak or dead */
    float contrast;     /* (p99 - p01) / 255: fog and dirt flatten it */
    float tenengrad;    /* mean squared Sobel magnitude over pixels above the noise floor */
    float lapvar;       /* variance of the 4-neighbour Laplacian */
    float edge_frac;    /* fraction of pixels whose Sobel magnitude exceeds the noise floor */
};

/* circle_radius as in imgdiff_cfg (fraction of the width, 0 = whole frame) */
void imgqual_measure(const uint8_t *img, int w, int h, float circle_radius, struct imgqual *q);

/* LED control: the new PWM percentage for the next shot, from the current one and the measured
 * histogram. Starts from maximum current (the scene must be lit to 2 m) and only backs off when
 * highlights clip; raises again when the frame is dark. Returns cur when no change is needed. */
unsigned imgqual_led_step(const struct imgqual *q, unsigned cur_percent);

enum imgqual_verdict { IQ_OK = 0, IQ_TOO_DARK, IQ_SATURATED, IQ_BLURRED, IQ_LOW_CONTRAST };

/* Compare with the reference frame's measurement (same camera, same LED setting). */
enum imgqual_verdict imgqual_judge(const struct imgqual *cur, const struct imgqual *ref);

#endif
