/*
 * Change detection between two luminance images of the same fisheye camera, at the camera's native resolution
 * (user 2026-11-12: "low resolution will not see any leak"; at 320x240 a 10 cm puddle 3 m away was ~2 pixels in a
 * 20x20 block). The images are masked to the lens' image circle and gain-normalised (a slightly different exposure
 * or light level is not a change); the decision is per block of IMGDIFF_BLOCK x IMGDIFF_BLOCK pixels (~9 cm at 3 m):
 * changed when its mean difference is high (a large change) or when enough of its pixels changed (a small one: a 10 cm
 * puddle at 3 m is ~17x10 pixels, its mean diluted over the blocks it straddles; test144). Noise stays under both.
 */
#ifndef LEAKCAM_IMGDIFF_H
#define LEAKCAM_IMGDIFF_H

#include <stdbool.h>
#include <stdint.h>

#define IMGDIFF_W        2592            /* the OV5647's native width, 1936 of its 1944 rows (cap.h, job.h) */
#define IMGDIFF_H        1936
#define IMGDIFF_BLOCK    16
#define IMGDIFF_BLOCKS_X (IMGDIFF_W / IMGDIFF_BLOCK)
#define IMGDIFF_BLOCKS_Y (IMGDIFF_H / IMGDIFF_BLOCK)

struct imgdiff_cfg {
    float circle_radius;   /* image-circle radius as a fraction of the width (0 = no mask) */
    int   pixel_thresh;    /* |difference| that counts a pixel as changed, 0..255 */
    float block_thresh;    /* mean |difference| over a block that counts the block as changed */
    int   block_pixels;    /* changed pixels in a block that count the block as changed (a small change) */
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

/* ref: IMGDIFF_W x IMGDIFF_H packed (the history); cur: the same size with row stride cur_stride (the camera's frame,
 * read in place) */
void imgdiff_compare(const uint8_t *ref, const uint8_t *cur, int cur_stride, const struct imgdiff_cfg *cfg,
                     struct imgdiff_result *out);

#endif
