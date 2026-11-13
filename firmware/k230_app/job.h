/*
 * The leak detection job (DESIGN.md 5.3.8, 5.3.12): its own thread beside the USB relay. One check at the start and one
 * per CHECK from the BL616: both cameras captured, each compared with its history. The relay owns the link: it takes
 * the result here and sends DONE,<cam0>,<cam1>,<foc0>,<foc1> (k230_link.h).
 */
#ifndef LEAKCAM_JOB_H
#define LEAKCAM_JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "cap.h"

#define JOB_CAMS 2                      /* 0 = CSI0 CAM2 (J4), 1 = CSI2 CAM1 (J5) */
#define JOB_W    2592                   /* the cameras' frame (vicap_cap.c: the OV5647's native mode) */
#define JOB_H    1936                   /* 1944 cropped to whole 16-row blocks (the JPEG encoder, vicap_cap.c) */

/* the thread, its first check at once */
void job_start(void);
/* one more check (the BL616's CHECK,<reason>: a probe or the RTC while the K230 runs) */
void job_request(const char *reason);
/* a finished check's result for DONE ("<cam0>,<cam1>,<foc0>,<foc1>": each camera same / light / changed / new / nocam /
 * unfocused, then its sharpness in % of its focus reference, -1 none; DESIGN.md 5.3.17); true once per check */
bool job_result(char *buf, size_t len);

/* one shot of every camera for the pictures (media.c): fn gets each camera's frame mapped (cap.h cap_frame_fn: native
 * NV12, no copy), the LEDs already off; *taken the moment (CLOCK_REALTIME, UTC); the bit mask of the cameras that gave
 * one (bit slot), 0: none. Waits for a running leak check: the cameras are one user's at a time */
int job_shoot(cap_frame_fn fn, void *ctx, struct timespec *taken);
/* job_shoot's shot, then up to `more` frames per camera as they come, until they span more_ms (a video: the LEDs on to
 * the last when lit), each to fn (y, uv NULL after the first) */
int job_video(unsigned more, unsigned more_ms, cap_frame_fn fn, void *ctx, struct timespec *taken);
/* the focus session (focus.c, DESIGN.md 5.3.17): both cameras streaming at once (a switch on the phone is instant: no
 * camera restarts), the LEDs on, their exposures settled once then held, every frame to fn while go(ctx); the luma
 * mapped only for the camera (slot) mapped(ctx) names, -1 none (the others' y NULL: the encoder takes the frame
 * itself); waits for a check or pictures (the cameras are one user's at a time) and holds them off meanwhile; 0, -1:
 * no camera or one failed */
int job_focus(cap_frame_fn fn, void *ctx, bool (*go)(void *ctx), int (*mapped)(void *ctx));
/* camera cam's histories deleted (its focus moved: the next check stores new ones, "new") */
void job_forget(int cam);
/* the last shot's moment (CLOCK_REALTIME), set before the frames go to fn: for fn itself */
void job_shot_time(struct timespec *t);

#endif
