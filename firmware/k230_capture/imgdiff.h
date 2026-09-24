/*
 * Change detection between two luminance images of the same fisheye camera.
 *
 * Both images are reduced to a small working size (default 320x240, a 4x4 box from the 1280x960
 * capture), smoothed 3x3, masked to the lens' image circle, and gain-normalised so a slightly
 * different exposure or LED level does not count as a change. The decision is made per block
 * (16x12 grid): a real object or water appearing moves a whole block, sensor noise does not.
 */
#ifndef LEAKCAM_IMGDIFF_H
#define LEAKCAM_IMGDIFF_H

#include <stdbool.h>
#include <stdint.h>

#define IMGDIFF_W        320
#define IMGDIFF_H        240
#define IMGDIFF_BLOCKS_X 16
#define IMGDIFF_BLOCKS_Y 12

struct imgdiff_cfg {
    float circle_radius;   /* image-circle radius as a fraction of the working width (0 = no mask) */
    int   pixel_thresh;    /* |difference| that counts a pixel as changed, 0..255 */
    float block_thresh;    /* mean |difference| over a block that counts the block as changed */
    float frac_thresh;     /* fraction of masked pixels changed that alone flags a change */
    int   dark_floor;      /* pixels darker than this in both images are ignored (vignette) */
};

struct imgdiff_result {
    bool  changed;
    int   changed_blocks;  /* blocks over block_thresh */
    float max_block_mad;
    float changed_frac;    /* changed pixels / masked pixels */
    float mean_ref, mean_new, gain;
    int   usable_blocks;   /* blocks at least a quarter inside the image circle */
    uint8_t block_changed[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X];   /* 1 = over block_thresh */
};

void imgdiff_default_cfg(struct imgdiff_cfg *cfg);

/* src: w x h luminance with row stride; dst: IMGDIFF_W x IMGDIFF_H. w and h must be integer
 * multiples of the working size. */
int imgdiff_reduce(const uint8_t *src, int w, int h, int stride, uint8_t *dst);

/* ref and cur are IMGDIFF_W x IMGDIFF_H outputs of imgdiff_reduce() */
void imgdiff_compare(const uint8_t *ref, const uint8_t *cur, const struct imgdiff_cfg *cfg,
                     struct imgdiff_result *out);

#endif
