/*
 * The cameras' videos on the VPU (user 2026-11-12: "h265 with native resolution 15 frames"; DESIGN.md 5.3.14): one
 * H.265 Main channel per camera (VENC 1 + cam; 0 is the JPEG encoder's, jpeg_hw.c), CBR, an IDR every second (the
 * server's live HLS cuts its segments there, and a clip starts playing from any second). Fed the camera's own frames
 * as they stream (kd_mpi_venc_send_frame: no copy, no stream restart); opened for a clip and closed after it (the VPU's
 * working memory comes back).
 */
#ifndef LEAKCAM_H265_H
#define LEAKCAM_H265_H

#include <stddef.h>
#include <stdint.h>

#define H265_KBPS 10000                 /* per camera (user 2026-11-12: "video looks not detailed" at 2000): 5 MP at
                                         * 15 fps; the link carries 27.5 Mbit/s up (test100); lwIP's heap was the
                                         * limit (1600 B, install.sh) */

/* channel for camera cam at w x h, fps; 0 or -1 */
int h265_open(int cam, unsigned w, unsigned h, unsigned fps);

/* one frame (a VICAP k_video_frame_info) in, its access unit out (Annex-B, malloc'd into *out, *idr set): its
 * length, 0 when none came */
size_t h265_frame(int cam, const void *frame, uint8_t **out, int *idr);

/* the next frame an IDR (the focus session's camera switch: the other camera's first frame decodes on its own) */
void h265_idr(int cam);

void h265_close(int cam);

#endif
