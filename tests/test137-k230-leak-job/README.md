# test137: the K230 leak job with simulated cameras (product pair: the K230 program's job + its link frames)

Status: **pass** (2026-11-07 08:34, run.py 11/11, BL616 full 0.6+g2961d6c-dirty, K230 0.6+g2961d6c-dirty): every
scene gives its verdict on both cameras (new / same / light = same / object = changed), the history kept on a change,
CHECK -> DONE 111 ms (the job itself 48 ms), the change at the server 2 s after the check (a check-in at once), no
memory growth over 8 checks.

The leak detection job of the one K230 program (DESIGN.md 5.3.8, 5.3.12; firmware/k230_app/job.c) until the cameras
arrive: both cameras simulated (a synthetic room per camera, the scene picked by /sdcard/leakcam/sim), each frame
compared with its history (imgdiff), the result sent to the BL616 as `DONE,<cam0>,<cam1>`. On USB power (the bench),
the K230 on the whole time: each check is the BL616's `CHECK` to the running K230 (`leak check` on the BL616 console,
as the RTC's), answered by the job's DONE; the result in the next check-in (`job=`, `jago=`), at once on a change.

## How it works

- The K230 program starts the job thread beside the USB relay: one check at the start, one per `CHECK,<reason>`.
- A check: per camera a 320x240 frame from the simulated camera (scene same / light / object), smoothed as a real
  capture, compared with /sdcard/leakcam/cam<N>.hist: no history = `new` (written); imgdiff (blocks, gain-normalised)
  says `changed` or `same`. A change of the light level alone reads as `same` (gain normalisation; the change net's
  `light` verdict comes with the KPU step). The history is kept on `changed`.
- The scene file: the last `cam<N>=<scene>` counts (the K230 console's `echo` appends, it cannot create a file; the job
  creates it empty). `rm` it: every camera `same`.
- The BL616 (firmware/bl616_full/leak_check.c): `CHECK` to the running K230, `DONE` logged (`leak: DONE <r> <ms> ms
  after the ask`), kept for the check-in (metrics_job), a check-in at once when a camera `changed`; on USB the K230
  stays on.

## Setup

Product BL616 pair + product K230 image; USB-C 1 on the bench (USB power: the K230 on); the server on the Pi.

## Steps

1. **No history:** the histories and the scene file removed; `leak check` -> `DONE new,new`, both histories written
   (76800 B each).
2. **Same scene:** `leak check` -> `DONE same,same`.
3. **Light only:** cam0 40 % darker -> `same,same` (no change reported for light alone).
4. **An object (water on the floor) on cam0:** -> `changed,same`; the server has `job=changed,same` within 30 s
   (the check-in at once, not the next interval).
5. **Back and on the other camera:** cam0 same, cam1 object -> `same,changed`.
6. **History kept on a change:** cam1 same again -> `same,same` (the history was not overwritten by the change).
7. **Repeated checks, memory:** 8 checks in a row; the K230 program's heap growth (`job: done: ... heap +<B>`) and the
   BL616's free heap do not grow with the count.
8. **Speed:** the time from `CHECK` to `DONE` (the BL616's `leak: DONE ... ms after the ask`) and the job's own time
   (`job: check` -> `job: done` on the K230 console).

## Pass

Every scene gives its verdict on both cameras, the history only on `new`, the result reaches the server at once on a
change, no memory growth over repeated checks.

## Needs (build features and dependencies)

K230: job.c, imgdiff.c (from attic/firmware/k230_capture), the relay's CHECK/DONE. BL616 full: leak_check.c
(`leak check`, DONE, metrics_job), k230_session_check. Server: job/jago metrics.

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: the K230 runs already (USB power); its boot is test045/test113's | test022 | |
| This part's boot share | the job thread's start, first check at t ~640 ms after the K230 start | - | |
| Test run time | 18 s automated (run.py, 2026-11-07), no setup | - | |
| Check: CHECK -> DONE | 111-211 ms (10 checks), median 111 ms | - | BL616 log |
| Job: capture + compare, both cameras | 48 ms per check, both cameras (simulated capture + 3x3 smoothing + imgdiff each) | - | K230 console steps |
| Current idle / during | n/a: on USB power, not metered | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 183312 -> 184052 B, min 165024 over 8 checks; K230 program heap +0 B after every check: no leak | no leak | over 8 checks |
| Image (sizes) | BL616 full 1110800 B; K230 program /app/leakcam 71136 B (job + imgdiff in it) | minimal image (rule) | |
| Speed / rate against the top | CHECK -> DONE 111 ms: the job 48 ms, the rest the link and the relay loop's 100 ms poll (DONE waits for it); a first check (histories written) 211 ms | SPEEDS.txt | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power, not metered | - | |
| Per day | n/a: not metered; the battery check is test132 | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: the job runs ~0.1 s, no heat change | K230 limit (datasheet) | |
| Compile flags | K230 program -Os -march=rv64imafdcv, sections + --gc-sections, strip; BL616 -Os -g | smallest code (rule) | |
| Features over the minimal image | job.c + imgdiff.c (K230), leak_check.c (BL616 full) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
