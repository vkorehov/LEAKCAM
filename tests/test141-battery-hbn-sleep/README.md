# test141: the full firmware on battery sleeps in HBN between its events (whole product flow, product firmware)

Status: **not run yet** (written 2026-11-10).

DESIGN.md 5.3.12 (user, 2026-11-07: HBN between events on battery) and 5.3.15: once nothing runs (no K230, no leak
check, no check-in) and the next event is 10 s or more away, the full firmware on battery enters HBN level 0
(hbn_sleep.c) until the earliest of the check-in, the leak check and the humidity reading (RTC), a probe edge (ACOMP1)
or the USB plug (ACOMP0). Every wake is a reset: the dues, the probe state, the missed check-ins (hbn_sleep.c), the
cool window (climate.c), the wall clock (wall_clock.c) and the metrics' ages (metrics.c) are kept in HBN RAM against
the RTC (rtc_clock.c). A wake counts as `hbnw` in the check-in, not as a boot.

## Setup

Product BL616 pair + product K230 image, USB-C 1 on the bench relay box, the server on the Pi. Hands-free: the server's
check-in interval 20 s for the run (PUT intervals), the cool window 30 s (`climate quiet 30`, kept through the sleeps),
USB-C 1 out (sleep on: phone.KEEP_AWAKE False, no `hbn off`) and back. The BL616's console disappears while it sleeps
(its USB device sleeps with it). Every other test that unplugs keeps the board awake (`hbn off`, phone.hand()).

## Steps

1. **Asleep:** USB-C 1 out: the console gone within a minute (the K230 off, the first check-in and reading done).
2. **Check-ins through the sleep:** the server sees check-ins ~20 s apart; the check-in carries `hbnw` >= 1 and the
   same `boots`; a check-in wake after 30 s cool carries a fresh room reading (`troom`, `roomago` <= 5 s).
3. **The USB plug wakes it:** the console back within seconds, `hbn` says "a wake from HBN", the wall clock within 5 s
   of the PC's (kept through the sleeps without a reset of the clock).

## Pass

The board sleeps on battery when nothing runs, wakes on the RTC for every check-in on the server's interval and on the
USB plug, and keeps its schedule, counters, room reading and clock through every wake.

## Result

Not run yet.

## Needs (build features and dependencies)

BL616 full: hbn_sleep.c, presence.c / leak_check.c on the RTC schedule, climate.c (HBN RAM state, the reading at the
check-in), rtc_clock.c, wall_clock.c on the RTC, metrics.c (`hbnw`, RTC ages). Server: `hbnw` kept (MetricKeys).
The probe wake (ACOMP1) needs the probe wet by hand: test132.

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: the wake's boot is not timed here | test021 | |
| This part's boot share | n/a | - | |
| Test run time | 102 s automated (run.py, 2026-11-10), no setup | - | |
| HBN cycle | check-ins [22, 22, 22] s apart asleep between them, hbnw 1 since the last good check-in, sleeping 84 s on battery | - | check-ins asleep between them |
| Current idle / during | n/a: no meter hands-free (sleep floor 36 uA: test031) | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: every wake is a reset (no heap carried) | no leak | |
| Image (sizes) | n/a | minimal image (rule) | |
| Speed / rate against the top | n/a | SPEEDS.txt | |
| Voltage under load (min) | n/a: no meter | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: no meter; DESIGN 5.3.12 budget | - | |
| Per day | n/a: no meter | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: test140 | K230 limit (datasheet) | |
| Compile flags | BL616 -Os -g | smallest code (rule) | |
| Features over the minimal image | hbn_sleep.c, rtc_clock.c | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: every HBN wake boots the BL616 (RF calibration, the check-in); timed in a later energy run (test102 style).

## Battery energy

DESIGN.md 5.3.12's budget (~2.85 mAh/day); measured with the meter by hand (test102), not here.
