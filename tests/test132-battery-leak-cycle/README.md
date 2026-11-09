# test132: the leak checks on battery (whole product flow, product firmware)

Status: **not run yet** (2026-11-07): needs the hands (USB-C 1 out and in, the probe wet and dry). The HBN sleep
between the events is not built: the full firmware stays awake on battery, so the sleep current and the day's energy
come with that step.

DESIGN.md 5.3.12 (user, 2026-11-07): on battery the full firmware powers the K230 only for a leak check: the RTC's
check on the server's `leakcheck` interval (default a day) and a wet probe at once (ACOMP1 below 1.10 V). The K230's job
answers `DONE,<cam0>,<cam1>`, the BL616 keeps it for the check-in, checks in at once on a change or a wet probe and
cuts the K230's power. Cameras simulated (test137).

## Setup

Product BL616 pair + product K230 image, cell charged; USB-C 1 on the bench at the start (the K230 on), the BL616
console on its own port (stays plugged); the server on the Pi (bench check-in 60 s). No phone.

## Steps

0. On USB: fresh histories, the scene cam0 = object (each battery check reports `changed,same`).
1. **Hand: USB-C 1 out.** The battery read by the running K230 at once, the K230 off, the server's `chg` 0 at once.
2. **The RTC's check on battery:** leak check interval 60 s (PUT): a K230 session `rtc`, `DONE changed,same`, the
   K230 off right after DONE (`k230: off (leak check done)`, its rails-on time), the server has `job=changed,same` at
   once, the check-in's `ks` >= 1, `klast` 1.
3. **Hand: the probe wet:** `leak: probe wet`, a K230 session `leak`, DONE, the K230 off; the server's `probe` below
   1100 mV at once.
4. **Hand: the probe dry:** `leak` says dry.
5. **Hand: USB-C 1 back:** the server's `chg` 1 at once, the K230 on again (USB).
6. (With the HBN step) the sleep between the events: the sleep current, the RTC wake for the check-in and the leak
   check, the probe wake from HBN; the day's energy against DESIGN.md 5.3.12's ~2.85 mAh.

## Pass

On battery the K230 runs only for each check and goes off right after its DONE; a change and a wet probe reach the
server at once.

## Needs (build features and dependencies)

BL616 full: leak_check.c (probe ACOMP1, the RTC check, the power cut after DONE, the 60 s cap), usb_full.c (the unplug
edge, no cut while a check runs). K230: job.c. Server: intervals, job/jago. Later: the HBN sleep and its wakes.

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | **MISSING** | test022 | the K230's boot inside each check |
| This part's boot share | **MISSING** | - | |
| Test run time | 542 s automated (run.py, 2026-11-07), no setup | - | |
| Battery leak check: rails on | 1305 ms rails on per check (power-on -> off after DONE), DONE 997 ms after the ask | - | power-on -> off after DONE |
| Current idle / during | **MISSING** | floor (test031) | the cell lead meter, if available (no scope, bench limits) |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak | |
| Image (sizes) | n/a: see test137 | minimal image (rule) | |
| Speed / rate against the top | **MISSING** | SPEEDS.txt | |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | the K230's reading in the check |
| Power, energy per operation | **MISSING** | - | per leak check |
| Per day | **MISSING** | ENERGY.txt line | with the HBN step |
| Temperature die / board (start / peak / end) | n/a: ~1 s sessions | K230 limit (datasheet) | |
| Compile flags | BL616 -Os -g; K230 program -Os | smallest code (rule) | |
| Features over the minimal image | leak_check.c, the session's power cut | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
