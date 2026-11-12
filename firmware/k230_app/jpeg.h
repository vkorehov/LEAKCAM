/*
 * The moment a picture was taken, in its EXIF: the APP1 segment (DateTimeOriginal + OffsetTimeOriginal "+00:00": UTC,
 * as every LEAKCAM time) for the hardware encoder's JPEGs (jpeg_hw.c inserts it right after the SOI).
 */
#ifndef LEAKCAM_JPEG_H
#define LEAKCAM_JPEG_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* the segment (marker FFE1 included, ~100 B) for `taken` into dst: its length, 0 when it does not fit cap */
#define JPEG_EXIF_MAX 128
size_t jpeg_exif(time_t taken, uint8_t *dst, size_t cap);

#endif
