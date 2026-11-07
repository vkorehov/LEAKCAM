# test103: Whole leak pipeline, end to end

Status: **later** (2026-11-07): not run yet; needs the cameras (until they arrive the K230's job simulates them,
job.c) and the full firmware's leak check on battery (leak_check.c).

Every part measured on its own (KPU 0.5 ms, SDIO, NAND, encoder): the time from a leak-check wake (RTC once a day,
or a wet probe) to the result at the server and, on a leak, the clip, with everything running together (DESIGN.md
5.3.12).

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Wake (`rtc` or `leak`) -> K230 READY (test101) -> WAKE -> the job: capture both cameras -> imgdiff -> change net
   -> history -> `DONE,<cam0>,<cam1>` -> the BL616 cuts the rails (on battery) -> its check-in with the result (`job=`,
   `jago=`) -> the server's answer -> (a clip asked) RTMP clip start: timestamps at each step on both consoles
   (console_record.py).
2. The results per camera against the history: `same`, `light`, `changed`, `new`; the BL616 checks in at once on
   `changed` or a wet probe, otherwise the result waits for the next check-in (the server's `checkin`, default 3600 s).
3. A wet probe (ACOMP1 below 1.10 V) with no visible change; the server not reached (the result kept for the next
   check-in); a K230 already on (USB) gets `CHECK,<reason>` instead of a session.
4. Battery cost: with test102's current, the energy per leak check for each path (estimate ~0.3 mAh, DESIGN.md
   5.3.12).

## Pass

Recorded timeline; the longest step named; the leak-to-video time within what the user expects.

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| leak -> video steps | **MISSING** | - | % of the top |
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

Boot share of the leak-to-video time (test022).

## Battery energy

Energy per leak check (wake to `DONE` and power-off) and per leak event (to the clip uploaded and power-off).
