# test103: Whole leak pipeline, end to end

Status: **later** (2026-10-21): not run yet; excluded for now (user): needs the cameras, Wi-Fi and the product BL616
firmware.

Every part measured on its own (KPU 0.5 ms, SDIO, NAND, encoder): the time from a wake to the server's answer,
with everything running together.

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Wake -> K230 READY (test101) -> capture both cameras -> change net -> history -> POST /v1/check -> answer ->
   (leak) RTMP clip start: timestamps at each step on the K230 console (console_record.py).
2. Same with the server answering "leak" (the clip path) and "no leak" (back to sleep).
3. The decisions `leakcam_wake` takes on the board: same / light / new per camera against the history, a sensor alarm
   without a visible change, no server, server not reached; the sleep it sends each time (6 h / 1 h / 10 min).
4. A change net offered in the answer: `GET /v1/nn/<CRC>` downloaded, stored, used on the next wake.
5. Battery cost: with test102's current, the energy per wake for each path.

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

Energy per leak event, wake to clip uploaded and power-off.

