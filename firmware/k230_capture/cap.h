/*
 * Multi-camera capture, one frame per camera: the interface leakcam_wake uses, implemented in
 * vicap_cap.c on MPP VICAP (node = camera slot, 0 = CSI0 CAM2/J4, 1 = CSI2 CAM1/J5).
 */
#ifndef LEAKCAM_CAP_H
#define LEAKCAM_CAP_H

#include <stddef.h>
#include <stdint.h>

#define CAP_MAX_CAMS 3
#define CAP_NBUF     4

#define CAP_DEFAULT_NODES { 0, 1 }     /* VICAP devices 0 (CSI0) and 1 (CSI2) */

struct cap_cam {
    unsigned node;                 /* backend's camera handle, see above */
    unsigned slot;                 /* set by the backend: 0 = CSI0 CAM2 (J4), 1 = CSI2 CAM1 (J5) */
    unsigned width, height, stride;
    void *buf[CAP_NBUF];
    size_t len[CAP_NBUF];
    uint8_t *luma;                 /* width x height copy of the Y plane of the kept frame */
    unsigned frames;               /* frames dequeued so far */
};

/* open + format every camera (sorted by node), then map and queue buffers */
int cap_open_all(struct cap_cam *cams, int n, unsigned width, unsigned height);
/* stream all, drop `settle` frames per camera (AE/AWB convergence), keep the next one */
int cap_grab_all(struct cap_cam *cams, int n, unsigned settle, int timeout_ms);
void cap_close_all(struct cap_cam *cams, int n);

#endif
