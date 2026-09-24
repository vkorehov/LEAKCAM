/*
 * Multi-camera capture, one frame per camera: the interface leakcam_capture uses. Two backends:
 *   v4l2cap.c   Linux, k230_linux_sdk vvcam V4L2 stack (node = /dev/video<node>)
 *   vicap_cap.c RT-Smart, k230_sdk MPP VICAP (node = camera slot, 0 = CSI0 CAM2/J4, 1 = CSI2 CAM1/J5)
 *
 * V4L2 backend:
 * Rules taken from the SDK's v4l2-drm-three-camera sample:
 *   - the ISP pipeline is created on VIDIOC_G_FMT, which can fail for a moment: retry it;
 *   - open and format ALL cameras first, in /dev/videoN order, before queueing or streaming;
 *   - single-planar V4L2_BUF_TYPE_VIDEO_CAPTURE, NV12. MMAP buffers are used here (no display).
 * Node map: slot 0 = /dev/video0..2 (MP, SP1, SP2), slot 1 = video3..5, slot 2 = video6..8.
 */
#ifndef LEAKCAM_CAP_H
#define LEAKCAM_CAP_H

#include <stddef.h>
#include <stdint.h>

#define CAP_MAX_CAMS 3
#define CAP_NBUF     4

#ifdef LEAKCAM_RTSMART
#define CAP_DEFAULT_NODES { 0, 1 }     /* VICAP devices 0 (CSI0) and 1 (CSI2) */
#else
#define CAP_DEFAULT_NODES { 0, 3 }     /* ISP port 0 = /dev/video0, port 1 = /dev/video3 */
#endif

struct cap_cam {
    unsigned node;                 /* backend's camera handle, see above */
    unsigned slot;                 /* set by the backend: 0 = CSI0 CAM2 (J4), 1 = CSI2 CAM1 (J5) */
    unsigned width, height, stride;
    int fd;                        /* V4L2 backend only */
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
