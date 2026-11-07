# test104: battery energy audit

Status: **planned** (2026-11-07): when the full firmware runs its battery day for days (HBN sleep between events, the
check-ins, the leak checks: DESIGN.md 5.3.12; the HBN sleep step is not built yet).

Adds up the per-part figures that every low-level test records (section "Battery energy" in each) in
[ENERGY.txt](../../ENERGY.txt), and checks the sum against a multi-day run. It finds no bottleneck itself: those
show in the low-level tests (user, 2026-10-21).

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Sleep current: from test031 (BL616 HBN with the test firmware, 35 uA: ~0.86 mAh/day).
2. Energy per check-in, the BL616 alone (HBN wake -> Wi-Fi -> `POST /v1/poll` -> HBN): test100's BL616-only check
   (~0.036 mAh, 1.71 s awake), x the server's `checkin` (default 3600 s: 24 a day) (no battery measurements (user,
   2026-10-30)).
3. Energy per leak check: a K230 session READY -> job -> `DONE` -> rails off (test101 timing, test102 current), boot
   part separate (test022); once a day by the RTC (the server's `leakcheck`, default 86400 s) plus one per wet probe.
4. Energy per leak event with a clip upload (Wi-Fi).
5. Budget: sleep + 24 check-ins + 1 leak check a day (+ leaks), self-discharge (~0.8 mAh/day); estimate ~2.85 mAh/day
   (DESIGN.md 5.3.12: ~0.9 years on one cell, ~1.8 on two); battery life for 1 and 2 cells from the measured sum.

## Pass

ENERGY.txt filled with measured figures; the budget and the long run agree within ~20 %; battery life recorded.

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - battery lead over whole sessions; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

The boot's share of the daily energy (test022 x wakes per day).

## Battery energy

This test is the audit: ENERGY.txt filled, battery life.
