/*
 * The change net on the KPU: is what imgdiff saw a real change of the scene, or only light?
 *
 * leakcam_change.kmodel (firmware/k230_nn) is MobileNetV2-0.35 with its ImageNet weights up to
 * block_13_expand_relu, int16: the reduced luma frame (IMGDIFF_W x IMGDIFF_H) in, a 15x20x192
 * feature map out. The distance of two frames is the max over the 300 cells of (1 - cosine) of
 * their features. Below the camera's threshold (CHANGE_THRESHOLD, or the one the server sent with
 * an update, nnstore.h) the change is light only and the wake goes back to sleep.
 */
#ifndef LEAKCAM_CHANGE_H
#define LEAKCAM_CHANGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CHANGE_KMODEL                  /* the host test points it elsewhere */
#define CHANGE_KMODEL    "/sdcard/app/leakcam_change.kmodel"   /* the factory model */
#endif
#define CHANGE_THRESHOLD 0.44f          /* the factory model's, both cameras (k230_nn/make_embed.py) */

/* the kmodel, kept by the caller until exit (the runtime does not copy it): 0, or -1 (logged) */
int change_load(const uint8_t *kmodel, size_t len);
/* both frames IMGDIFF_W x IMGDIFF_H luma, as imgdiff_reduce() gives them, after change_load().
 * The distance, or -1 (logged) if the net cannot run */
float change_distance(const uint8_t *ref, const uint8_t *cur);

#ifdef __cplusplus
}
#endif

#endif
