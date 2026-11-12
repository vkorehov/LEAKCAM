# test146: camera focus - bad focus detected, tuned by hand with the app's turn guide

Status: **step 1 built and passing (host, 2026-11-12)**: the app's turn guide (FocusGuide) brings a simulated lens to
its sharpest point from either side, at any hand speed of 1-4 degrees per score and board noise up to 5 % (7 unit
tests, 18 speed / noise / side combinations). The focus screen (FocusActivity) builds. Steps 2-6: the K230 session, score, judgement,
server push and app PIN step built 2026-11-12 (K230 image, BL616 pair, server `go test`, app build all pass), not yet
run on the board.

Why (user 2026-11-12): "we need to determine bad focus ... the KPU can have a separate output saying it cannot
reliably calculate differences because no focus, in that case ask the user to start the focus tuning procedure",
"help user with direction where to rotate lens; if focus gets worse swap direction", "good UX for user to adjust
focus asap". A camera out of focus makes the leak comparison blind: a 10 cm puddle at 3 m is ~17x10 pixels when
sharp (tests/test144), a soft smear when not.

## How it works (DESIGN.md 5.3.17)

- The K230 scores sharpness per camera (Tenengrad over 5 areas, brightness-normalised) on every LED-lit leak check
  and, in a focus session, on every frame; below 60 % of the camera's reference on 3 checks in a row:
  `unfocused` in `job=`; the change net's `unreliable` output says the same for fog, smudges and a covered lens.
- The server notifies the phone once; the notification opens the focus screen for that camera.
- The focus screen runs over the USB cable: `FOCUS,<cam>` -> `FOC,<cam>,<seq>,<score>` ~15 a second and a preview
  JPEG once a second; the guide says which way to turn (↺ / ↻), when it went too far, when to stop; Done saves the
  reference (`FOCUS,<cam>,DONE,<score>` -> `FOCUS,OK`).

## Setup

- Steps 1: the host only (`cd android && ./gradlew testDebugUnitTest`, aapt2 override on this arm64 host:
  android/README.md).
- Steps 2-6: the board on the full firmware, the bench phone on the cable (the relay box: K1-K4 to the phone, skill
  leakcam-bench-relays), a textured target at ~1.5 m in front of camera 0, the lens reachable (cover off).
  No motor: the lens is turned by hand when the step says so (the runner says exactly what to do and waits for the
  score to move, memory leakcam-short-test-timeouts), or defocused by a known amount (a lens turned 90 degrees from
  its tuned point, marked with a pen line).

## Steps

1. **The guide (host, `FocusGuideTest`):** a simulated lens (sharpness peak at a position, the person turning the
   way the guide says, 1-4 degrees per score, noise 0-5 %): it ends on STOP within 15 degrees of the peak from either
   side; the other side starts with WRONG_WAY; starting at the peak still ends there; a flat scene (only noise) says
   NO_DETAIL; a jump says "slower".
2. **The score on the board:** `FOCUS,0` over the cable: FOC lines at >= 12 a second; the score steady within 3 %
   (the median of 5) with nothing moving; the lens turned 90 degrees off: the score falls below 60 %; LEDs on during
   the session and off after `FOCUS,STOP`; the K230 heap flat over 2 min of streaming.
3. **The screen with a hand:** from 90 degrees off, the person follows the screen: the first hint's direction, a
   wrong way told within 1 s of turning the wrong way, STOP within 5 % of the score at the marked tuned point, Done
   -> `FOCUS,OK`; under 60 s per camera (the UX target). Screenshots of each state (skill leakcam-app-ux section 6).
4. **Bad focus in a leak check:** the reference saved, the lens turned 90 degrees off: the 3rd LED-lit check reports
   `job=unfocused,...,<pct>,...` with the % below 60; the server sends one notification; tapping it opens the focus screen on
   camera 0. Tuned back: `foc` above 90, no notification, new histories written (`new`), no leak raised.
5. **Room light does not judge focus:** a room-lit check with the lens off: no `unfocused` from it (the LED-lit
   checks decide).
6. **The change net's `unreliable`** (when the net has the output, firmware/NN.txt): defocused, fogged (breath on the
   lens), covered: `unreliable` on each, `changed` / `same` on a sharp lens as before.

## Pass

Step 1: every unit test. Steps 2-6: every check above, the phone left clean (memory phone-clean-after-tests), the
reference and the histories restored at the end.

## Result

2026-11-12, step 1: `FocusGuideTest` 7/7 (the noisy and the speed / noise sweep cases needed two fixes first: the
best is the last pass's, and after a peak any fall means passed again: HISTORY.md).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | **MISSING** (step 2) | test022 | |
| This part's boot share | n/a: the focus session is not at boot | - | |
| Test run time | step 1: 12 s (gradle, host) | - | |
| Speed / rate against the top | **MISSING**: FOC lines per second (step 2) | 15 fps (the sensor) | |
| Current idle / during | **MISSING** (step 2: LEDs on for the session) | floor (test031) | |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING**: energy per tuning (1 min) | - | |
| Per day | n/a: a rare action | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** (step 2) | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** (step 2: 2 min of streaming) | no leak | |
| Image (sizes) | **MISSING**: app APK, K230 image | minimal image (rule) | |
| Compile flags | n/a until the K230 part is built | smallest code (rule) | |
| Features over the minimal image | the focus session (k230_app), FocusActivity + FocusGuide (app) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
