# test101: session length (wake to power-off)

Status: **pass** (2026-11-08, run.py on the product pair, 40 s): K230 cold session (`k230 off`, the full firmware's restart on USB power) 3V3 in 1.7 ms, first heartbeat 834 ms, READY 836 ms after the run; a leak check on USB (CHECK to the running K230 -> DONE) 109-110 ms. The 2026-10-30 numbers (test firmware's `pt_session`) are in HISTORY.md.

The whole session on battery: wake, K230 boot (measured on its own in test022), READY, WAKE, the leak job, `DONE`,
power-off. With test102 it decides the battery cost of a leak check.

## Setup

BL616 full firmware (firmware/bl616_full: k230_session.c, leak_check.c) on PR1, a network stored; the K230 product
image with `/app/leakcam` (firmware/k230_app) in the slot's ROMFS on NAND; both consoles (`/dev/ttyACM0`,
`/dev/ttyACM1`). The cut after `DONE` happens on battery only (on USB the K230 stays on, DESIGN.md 5.3.11): the
sessions run with USB-C 1 out; on USB the job's time from WAKE to `DONE` (`CHECK,<reason>` to the running K230) is
the part that can be timed. Cameras simulated until they arrive (job.c).

## Needs (build features and dependencies)

BL616: K230 power path, the session (k230_session.c: READY timeout, heartbeat watch, the session caps on battery),
the link (k230_link.h), the leak check (leak_check.c: `leak check`, the cut after `DONE`, JOB_MAX_MS 60 s); K230: the
boot chain (test022), `link_mux` (`/dev/link`), `/app/leakcam` (READY, WAKE, the USB relay, the job with simulated
cameras, `DONE`, `;b=` on every frame).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. On battery, `leak check` on the BL616 console at t0, both consoles recorded with timestamps (`console_record.py`):
   K230 milestones SPL, OpenSBI, RT-Smart banner, READY on the link, WAKE (reason logged), the job's start, `DONE`;
   the BL616's `k230: READY <ms> after the run`, `leak: DONE ...` and `k230: off (...) <ms> after power-on` (rails
   off).
2. Repeat 10 times (cold, rails off >= 5 s between).
3. On USB (K230 already on): `leak check` -> `CHECK,<reason>` -> `DONE`: the job's own time without the boot.

## Pass

Recorded: power-on -> READY (median, max) and what dominates it (DDR training, NAND read, mounts); READY -> DONE and
DONE -> rails off; first candidates to shorten.

## Result

Not re-measured on the product program yet (2026-11-07).

- Boot chain, 2026-10-30 (BL616 test firmware `pt_session`, 10 cold boots, spread <= 13 ms): from power-on U-Boot
  SPL 0.22 s, OpenSBI 0.33 s, RT-Smart banner 0.63 s, file systems mounted 0.84 s, msh 0.85 s, READY frame on the link
  0.87 s. The session after READY was the old agent's (HISTORY.md).
- The BL616's heap stayed level over those 10 sessions (131124 B free each time): no leak.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | power-on -> READY frame 0.87 / 0.87 s (median / max of 10; SPL 0.22, OpenSBI 0.33, RT-Smart 0.63, mounted 0.84, msh 0.85) (2026-10-30, test image; re-measure with /app/leakcam) | target (test022) | longest step: |
| This part's boot share | a leak check on USB (CHECK to the running K230 -> DONE): 0.11 s (median of 3); on battery the session adds the boot above and rails off | - | |
| Test run time | 1006 s automated (run.py, 2026-11-07), no setup | - | setup (flashing, plugging, waiting) separately |
| session length per step | **MISSING**: wake -> READY -> DONE -> rails off (the old agent session 27.68 s superseded: HISTORY.md) | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** for the job (test102); boot ~87 mA (held in reset, test021); 220 mA at msh idle (test006) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: no battery measurements (user, 2026-10-30); the K230's own reading is the `;b=` attribute | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING**: one leak check (estimate ~0.3 mAh, DESIGN.md 5.3.12) | - | scripts/energy.py |
| Per day | **MISSING**: one leak check a day on battery (the server's `leakcheck`, default 86400 s) plus one per wet probe | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** for the product program (2026-10-30 old session: board 36.3 / 39.2 / 38.9 C, AHT20) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 179988 / min 161116 / 180012 B, blocks 68 -> 67, deleted 0; leak: n/a, 4 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-08) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1326272 B (2026-11-07, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 -Os -g (SDK Kconfig default), no LTO (2026-10-30) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: the full firmware's session and leak check; /app/leakcam | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Boot is test022; this test adds the session after READY (WAKE, the job, DONE, the cut).

## Battery energy

Energy per leak check (with test102): the number the budget multiplies by the leak checks per day (DESIGN.md 5.3.12).
