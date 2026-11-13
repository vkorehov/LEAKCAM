/*
 * Camera focus (DESIGN.md 5.3.17, tests/test146; user 2026-11-12: "we need to determine bad focus", "separate screen
 * for focus tuning per camera"): the sharpness score, each camera's focus reference and its bad-focus count
 * (/sdcard/leakcam/focus), and the app's focus session through the server (focus_run, a "focus" capture).
 */
#ifndef LEAKCAM_FOCUS_H
#define LEAKCAM_FOCUS_H

#include <stdbool.h>
#include <stdint.h>

#define FOCUS_BAD_PCT   60              /* below this % of the reference ... */
#define FOCUS_BAD_N     3               /* ... on this many LED-lit checks in a row: unfocused */

/* the sharpness of a native luma frame (w x h, row stride): the brightness-normalised gradient energy (Tenengrad,
 * Sobel, / mean^2) of every 256x256 tile of a grid over the image circle (70 at 2592x1936: the whole picture but its
 * very corners), averaged, x 100. Compared only with itself (the same camera and scene); 0 for a black frame */
double focus_score(const uint8_t *y, unsigned stride, unsigned w, unsigned h);
/* the tiles of that grid */
unsigned focus_ntiles(unsigned w, unsigned h);
/* the sum of the tile scores (not x 100) of n tiles from tile `from`, every `step`-th; *count += the tiles scored.
 * The focus session spreads one whole-frame score over a second's frames with it */
double focus_tiles(const uint8_t *y, unsigned stride, unsigned w, unsigned h, unsigned from, unsigned n,
                   unsigned step, unsigned *count);

/* a LED-lit check's score of camera cam against its reference: the % kept for DONE, the bad count moved */
void focus_judge(int cam, double score);
/* the last judged % of the reference, -1: no reference yet */
int focus_pct(int cam);
/* FOCUS_BAD_N lit checks in a row below FOCUS_BAD_PCT */
bool focus_bad(int cam);

/* the focus session for the app, through the server (media.c: a capture of kind "focus"): both cameras streaming,
 * the shown one's H.265 and scores uploaded to http://host:port/v1/focus/<dev>/stream, the app's commands polled,
 * until stop or secs; *frames the pictures sent. 0, -1: no channel, no connection, or a camera failed */
int focus_run(const char *host, const char *port, const char *dev, int secs, unsigned *frames);

#endif
