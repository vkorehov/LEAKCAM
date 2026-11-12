/*
 * Camera focus (DESIGN.md 5.3.17, tests/test146; user 2026-11-12: "we need to determine bad focus", "separate screen
 * for focus tuning per camera"): the sharpness score, each camera's focus reference and its bad-focus count
 * (/sdcard/leakcam/focus), and the app's focus session over the USB cable (FOCUS lines, answered by the K230 itself).
 */
#ifndef LEAKCAM_FOCUS_H
#define LEAKCAM_FOCUS_H

#include <stdbool.h>
#include <stdint.h>

#define FOCUS_BAD_PCT   60              /* below this % of the reference ... */
#define FOCUS_BAD_N     3               /* ... on this many LED-lit checks in a row: unfocused */

/* the sharpness of a native luma frame (w x h, row stride): the brightness-normalised gradient energy (Tenengrad,
 * Sobel, / mean^2) over five 256x256 areas, the centre and four at half the image circle's radius (the fisheye's
 * field is curved), x 100. Compared only with itself (the same camera and scene); 0 for a black frame */
double focus_score(const uint8_t *y, unsigned stride, unsigned w, unsigned h);

/* a LED-lit check's score of camera cam against its reference: the % kept for DONE, the bad count moved */
void focus_judge(int cam, double score);
/* the last judged % of the reference, -1: no reference yet */
int focus_pct(int cam);
/* FOCUS_BAD_N lit checks in a row below FOCUS_BAD_PCT */
bool focus_bad(int cam);

/* a FOCUS line from the phone (the relay, after the PIN): FOCUS,<cam> start or switch the session, FOCUS,<cam>,DONE,
 * <score> keep the reference, FOCUS,STOP end. The answer line for the phone (NULL: none; the session's FOC and FIMG
 * lines go out from its own thread) */
const char *focus_line(const char *line);

#endif
