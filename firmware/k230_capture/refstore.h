/*
 * The reference image per camera on the K230's SPI NAND: <dir>/cam<N>.hist, what the stored
 * history shows (history.h), i.e. the last keyframe with every stored delta applied, reduced.
 * Every wake is compared with it, and it decides which blocks the next delta holds.
 *
 * Each reference is two slot files, <name>.0 and <name>.1: a 32-byte header (magic, version,
 * size, capture time, CRC-32, generation) plus the IMGDIFF_W x IMGDIFF_H luminance payload,
 * 76.8 KB. A save goes to the older slot (tmp file, fsync, unlink, rename, directory fsync) and
 * a load takes the newest slot whose CRC checks. That survives a power cut at any point on both
 * ext4/UBIFS and UFFS, where rename() cannot replace an existing file.
 */
#ifndef LEAKCAM_REFSTORE_H
#define LEAKCAM_REFSTORE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* 0 = loaded, 1 = no (valid) reference yet, -1 = I/O error */
int refstore_load(const char *dir, int cam, uint8_t *img, time_t *taken);
int refstore_save(const char *dir, int cam, const uint8_t *img, time_t taken);

uint32_t refstore_crc32(const uint8_t *p, size_t n);

/* the same two-slot scheme for other files (nnstore.c): */
/* n bytes or -1 */
int refstore_read_all(int fd, void *buf, size_t n);
/* path := a then b, durably: tmp file, fsync, unlink, rename, directory fsync. UFFS (the RT-Smart
 * NAND filesystem) refuses rename() onto an existing name, so the old file is unlinked first; a
 * power cut in between loses only that file, which is why callers keep two slots. 0 or -1 */
int refstore_replace(const char *dir, const char *path, const void *a, size_t na, const void *b, size_t nb);

#endif
