/*
 * JPEG on the K230's video encoder (VENC, MJPEG): a camera frame straight from its video buffer (no copy, no CPU),
 * the moment in EXIF (jpeg.c's segment, inserted after the SOI). The pictures for the server (media.c); a native
 * 2592x1944 picture in software took ~1.4 s of CPU (2026-11-12, that encoder removed).
 */
#ifndef LEAKCAM_JPEG_HW_H
#define LEAKCAM_JPEG_HW_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* frame: the camera's k_video_frame_info (cap_frame_fn's `frame`), NV12 width x height. The channel is made at the
 * first call for that size and quality and kept (a create / destroy per picture would repeat the VICAP leak pattern,
 * vicap_cap.c); made again when they change (the focus preview, focus.c). Thread-safe.
 * *out malloc'ed (the caller frees it); its length, 0 on failure */
size_t jpeg_hw(const void *frame, unsigned width, unsigned height, int quality, time_t taken, uint8_t **out);

/* the channel and its stream pool given back (its VPU pages: the two H.265 channels of a video need them, h265.c);
 * the next picture makes it again */
void jpeg_hw_close(void);

#endif
