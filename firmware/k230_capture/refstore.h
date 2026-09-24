/*
 * Reference images on the K230's SPI NAND (UBIFS), one file per camera and kind:
 *   <dir>/cam<N>.last   the previous wake's reduced image: "what changed since last time"
 *   <dir>/cam<N>.base   the baseline, set on first run or --rebaseline: slow drift, e.g. a
 *                       drip that grows a puddle a little each wake, stays visible here
 *   <dir>/cam<N>.hist   what the stored history shows (history.h): the last keyframe with every
 *                       stored delta applied, reduced; decides which blocks the next delta holds
 *
 * Each reference is two slot files, <name>.0 and <name>.1: a 32-byte header (magic, version,
 * size, capture time, CRC-32, generation) plus the IMGDIFF_W x IMGDIFF_H luminance payload,
 * 76.8 KB. A save goes to the older slot (tmp file, fsync, unlink, rename, directory fsync) and
 * a load takes the newest slot whose CRC checks. That survives a power cut at any point on both
 * ext4/UBIFS and UFFS, where rename() cannot replace an existing file.
 */
#ifndef LEAKCAM_REFSTORE_H
#define LEAKCAM_REFSTORE_H

#include <stdint.h>
#include <time.h>

enum ref_kind { REF_LAST, REF_BASE, REF_HIST };

/* 0 = loaded, 1 = no (valid) reference yet, -1 = I/O error */
int refstore_load(const char *dir, int cam, enum ref_kind kind, uint8_t *img, time_t *taken);
int refstore_save(const char *dir, int cam, enum ref_kind kind, const uint8_t *img, time_t taken);

uint32_t refstore_crc32(const uint8_t *p, size_t n);

#endif
