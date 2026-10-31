/*
 * Image history on the K230's SPI NAND: what each camera saw, stored as differences.
 *
 * The K230 loses its RAM at every power-off, so the history lives in files, one directory per
 * camera, one file per stored record:
 *
 *   <dir>/hist<N>/<seq>.K   keyframe: the whole full-resolution luminance frame
 *   <dir>/hist<N>/<seq>.D   delta: only the blocks that changed, full resolution, on top of the
 *                           keyframe <key_seq> and every delta between them
 *
 * Blocks are the change detector's 16 x 12 grid (80 x 80 px at 1280 x 960). A frame at sequence S
 * is rebuilt from its keyframe plus the deltas up to S. Payloads are zlib-compressed and carry a
 * CRC-32; files are written to a temporary name, fsync'd and renamed, so a power cut leaves either
 * the whole record or none. A new keyframe starts when too much of the image changed at once, or
 * after max_deltas deltas, so a lost file never costs more than one keyframe group. Over quota,
 * the oldest whole group (keyframe and its deltas) is deleted; the newest group is always kept.
 */
#ifndef LEAKCAM_HISTORY_H
#define LEAKCAM_HISTORY_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "imgdiff.h"

struct hist_cfg {
    const char *dir;        /* state directory; records go to <dir>/hist<cam>/ */
    size_t   quota_bytes;   /* per camera */
    unsigned max_deltas;    /* deltas per keyframe group */
    float    key_frac;      /* changed fraction of the usable blocks that forces a keyframe */
};

void hist_default_cfg(struct hist_cfg *cfg, const char *dir);

enum hist_kind { HIST_NONE = 0, HIST_KEY = 'K', HIST_DELTA = 'D' };

struct hist_info {
    enum hist_kind kind;    /* what was stored (HIST_NONE: nothing changed) */
    uint32_t seq, key_seq;
    unsigned nblocks;       /* blocks in a delta */
    size_t   bytes;         /* file size written */
    unsigned pruned;        /* files deleted for the quota */
};

/*
 * Store one frame: a keyframe when force_key is set or when the policy asks for one, otherwise a
 * delta of the blocks set in changed[][] (nothing when none is set). luma is w x h, tightly packed;
 * w and h must be multiples of the block grid. Returns 0 or -1 (errno set).
 */
int hist_store(const struct hist_cfg *cfg, int cam, time_t taken, const uint8_t *luma,
               unsigned w, unsigned h,
               const uint8_t changed[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X], int usable_blocks,
               int force_key, struct hist_info *out);

struct hist_entry {
    uint32_t seq, key_seq;
    enum hist_kind kind;
    time_t   taken;
    unsigned w, h, nblocks;
    size_t   bytes;
};

/* all records of a camera, oldest first; *n entries in a malloc'd array (caller frees) */
int hist_list(const char *dir, int cam, struct hist_entry **list, size_t *n);

/*
 * Rebuild the frame at seq (0 = newest) into a malloc'd w x h buffer (caller frees).
 * Returns 0, 1 when the record or its keyframe is missing or damaged, -1 on I/O error.
 */
int hist_rebuild(const char *dir, int cam, uint32_t seq, uint8_t **luma, unsigned *w, unsigned *h,
                 time_t *taken);

/* binary PGM, written atomically */
int hist_write_pgm(const char *path, const uint8_t *luma, unsigned w, unsigned h);

#endif
