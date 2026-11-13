# test149: a camera that sees nothing reads dark; ignored or tuned from the status screen (whole product flow, product firmware)

Status: **pass, 22/22** (2026-11-13 20:30 UTC, run.py, on USB power, the server on the PC, the bench phone).

DESIGN.md 5.3.18 (user, 2026-11-13: "dark frame should also be triggering unreliable capture and give user option
either to ignore camera completely or tune its focus in focus screen", "store ignored camera somewhere and skip it.
store in flash of k230"). A camera whose kept frame is nearly black (mean below 16 of 255) reads `dark` in the leak
job: not compared, the history kept, no leak. The server tells the phones once and flags the camera; the status screen
offers Tune focus and Ignore camera. An ignored camera goes to the board at its next check-in (a capture of kind
`ignore`), the K230 keeps the mask in `/sdcard/leakcam/ignore` and reads that camera as `ignored`.

## Setup

Product BL616 pair + product K230 image, the full firmware with a network stored, on USB power at the bench (no
hand), the server on the PC, the bench phone with the app (adb). The dark camera is simulated on the K230 console
(`echo cam0=dark /sdcard/leakcam/sim`), so the run does not depend on the bench's light.

## Steps

1. **Both cameras dark:** `leak check` reads `dark,dark`; the two history files untouched; the server has it and
   opens no leak.
2. **The status screen:** "Bottom camera sees nothing", with Tune focus and Ignore camera.
3. **Ignore camera:** the tap asks first; confirmed: the server has the bottom camera (0) ignored within 10 s; the flag moves on
   to the top camera; the board takes it at its next check-in and the K230's file holds mask 1.
4. **Not judged:** both ignored, an object simulated in both views (it would read `changed`): `ignored,ignored`, no
   leak, nothing left waiting for the board, the check not slower than the dark one.
5. **Used again:** "Use the bottom camera again" in the ⋮ menu; both in use: the K230's file 0, the next check `new,new`.
6. **After it:** no TCP connection left on the BL616, its heap not smaller.

## Pass

A dark camera never raises a leak, the person can ignore a camera or use it again from the app, and the K230 keeps
that choice and follows it.

## Result

2026-11-13 20:30 UTC: 22/22. `dark,dark` in 410 ms with no leak; Ignore camera from the status screen at the
server within ~11 s of the first tap (the dialog between), on the K230 at the next check-in (5 s with `presence now`);
`ignored,ignored` with objects in both views, no leak; Use the bottom camera again from the ⋮ menu; no TCP
connection left, the BL616's heap not smaller.

## Needs (build features and dependencies)

K230: job.c (`dark`, `ignored`, the `cam<N>=dark` test token, job_ignore), media.c (capture kind `ignore`). BL616:
capture.c (kind `ignore`). Server: alert.go (focusAlerts `dark`, reconcileIgnore, the capture), main.go (setIgnore).
App: MainActivity (the flag row, Ignore camera, Use camera again).

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: test021 | test021 | |
| This part's boot share | n/a | - | |
| Test run time | 98 s automated (run.py, 2026-11-13), no setup | - | |
| Current idle / during | n/a: no meter | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free (155264, 125644) -> (155264, 125644) (free, min); lwip: heap used 60 max 3916 avail 6144 err 0, tcp seg used 0 max 3 err 0, pbuf used 0 err 0 | no leak | |
| Image (sizes) | n/a | minimal image (rule) | |
| Speed / rate against the top | check: dark,dark 410 ms, ignored,ignored 411 ms | a judged check (test137) | |
| Voltage under load (min) | n/a: no meter | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: no meter | - | |
| Per day | n/a: no meter | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: test140 | K230 limit (datasheet) | |
| Compile flags | BL616 -Os -g, K230 app -O2 | smallest code (rule) | |
| Features over the minimal image | job.c dark / ignore | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
