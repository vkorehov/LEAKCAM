/*
 * Multi-camera capture, one frame per camera at the OV5647's native 2592x1944: the product job's (job.c), implemented
 * in vicap_cap.c on MPP VICAP (node = camera slot, 0 = CSI0 CAM2/J4, 1 = CSI2 CAM1/J5).
 */
#ifndef LEAKCAM_CAP_H
#define LEAKCAM_CAP_H

#include <stddef.h>
#include <stdint.h>

#define CAP_MAX_CAMS 3
#define CAP_NBUF     4

/* Exposure is ours, not the ISP's (auto exposure off, user 2026-11-12: "minimise energy waste": the ISP's AE took
 * 14-40 frames with the LEDs on, it starts each shot from the dark room's maximum). A shot sets the camera's last good
 * exposure, the LEDs go on (job.c), CAP_APPLY frames pass while the light and the setting land, then the frame is
 * measured: kept when its mean is within CAP_TARGET * [CAP_LOW, CAP_HIGH], else the exposure corrected by the ratio
 * (squared: the picture has gamma) and measured again CAP_APPLY + 1 frames later; at most settle + 1 frames. */
#define CAP_TARGET        90      /* mean of the sparse luma sample (0-255): the ISP's own AE settled at 82-100 */
#define CAP_LOW           0.75f
#define CAP_HIGH          1.30f
#define CAP_APPLY         2       /* frames between a change (LEDs, exposure) and the first frame showing it */
#define CAP_WARMUP        16      /* frames dropped after the stream starts (the sensor's first ~14 are black) */
#define CAP_BLACK         4       /* a mean below it is a black frame */
#define CAP_T_MIN         0.000065f   /* the native 2592x1944 15 fps mode: 2 .. 2039 lines of 32.5 us (66 ms), */
#define CAP_T_MAX         0.066f      /* analog gain 1 .. 8 (the SDK's ov5647 ae_info[0]) */
#define CAP_G_MIN         1.0f
#define CAP_G_MAX         8.0f
#define CAP_EXP_DEFAULT   0.010f  /* exposure x gain (s) before any shot was measured */
#define CAP_AMBIENT_MAX   0.132f  /* the room's light is enough without the LEDs when the target needs at
                                               * most this (66 ms at gain 2: noise averages out in the 320x240
                                               * compare, little motion blur) (user 2026-11-12: "adapt to the external
                                               * light, less LED needed by day") */

#define CAP_SMALL_W 320               /* channel 1: the whole picture scaled by the ISP, the leak check's size */
#define CAP_SMALL_H 240

#define CAP_DEFAULT_NODES { 0, 1 }     /* VICAP devices 0 (CSI0) and 1 (CSI2) */

struct cap_cam {
    unsigned node;                 /* backend's camera handle, see above */
    unsigned slot;                 /* set by the backend: 0 = CSI0 CAM2 (J4), 1 = CSI2 CAM1 (J5) */
    unsigned width, height, stride;
    void *buf[CAP_NBUF];
    size_t len[CAP_NBUF];
    unsigned frames;               /* frames dequeued in the last grab (the kept one is the last) */
    unsigned kept;                 /* the grab has its frame */
    unsigned mean;                 /* the kept frame's mean brightness (sparse sample, 0-255) */
    unsigned fw, fh;               /* the frame cap_release_all() hands over: its size (native, or 320x240) */
    uint8_t trace[64];             /* every frame's mean brightness in the last grab (the first 64): the AE curve */
    float exp;                     /* exposure time x analog gain (s): the last good one, the next shot starts from it
                                    * (0: CAP_EXP_DEFAULT); the caller may set it before the grab (job.c keeps it) */
    unsigned tries;                /* exposures set in the last grab (1 = the first guess was good) */
    unsigned ambient;              /* cap_ambient(): the room's light, mean of a frame with the LEDs off ... */
    float ambient_exp;             /* ... at this exposure */
};

/* open + format every camera (sorted by node), then map and queue buffers; each sensor starts at its cams[i].exp
 * (0: CAP_EXP_DEFAULT): the caller's persisted one, so a cold start probes the room as the running program would */
int cap_open_all(struct cap_cam *cams, int n, unsigned width, unsigned height);
extern unsigned cap_curve;         /* 1: no frame is kept before settle + 1, every mean traced (job.c's `curve` token) */
void rawpack_writers(void);        /* rawpack.c: the raw writers' line lengths packed, after the start */
/* the room's light now: a 320x240 frame per camera (the LEDs off: the caller's; the frame in flight skipped), its mean
 * at the exposure in use (c->ambient, c->ambient_exp); 2 frames. 0, -1: no frame */
int cap_ambient(struct cap_cam *cams, int n, int timeout_ms);
/* one frame per camera from the running streams (started by cap_open_all), small: the 320x240 channel (the leak
 * check), else the native one (pictures): the first whose brightness is in the
 * target band under the exposure set for it, at most settle + 1 frames; each kept frame stays held (no copy: a
 * native NV12 frame is 7.6 MB) until cap_release_all(), so the LEDs can go off before the frames are worked on */
int cap_grab_all(struct cap_cam *cams, int n, int small, unsigned settle, int timeout_ms);
/* a held frame, mapped: the luma (width x height, row stride `stride`) and the NV12 UV plane (height / 2 rows, the
 * same stride); valid only during the call */
typedef void (*cap_frame_fn)(const struct cap_cam *c, const uint8_t *y, const uint8_t *uv, unsigned stride,
                             const void *frame, void *ctx);   /* frame: its k_video_frame_info (the encoder's input) */
/* every held frame to fn (NULL: none), then released to the stream; 0, -1 when a frame could not be mapped */
int cap_release_all(struct cap_cam *cams, int n, cap_frame_fn fn, void *ctx);
/* a video: up to `frames` more frames per camera from the native streams (round-robin, at the rate they come), until
 * the first camera's frames span max_ms of their own time (pts), each to fn as it comes, then released; map: a bit per
 * camera (cams[i]: bit i) whose luma is mapped (the focus session's score; uv NULL), the others' y and uv NULL (the
 * encoder takes the frame itself); 0, -1: a camera gave none */
int cap_stream(struct cap_cam *cams, int n, unsigned frames, unsigned max_ms, unsigned map, cap_frame_fn fn, void *ctx,
               int timeout_ms);
void cap_close_all(struct cap_cam *cams, int n);

#endif
