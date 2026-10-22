# test103: Whole leak pipeline, end to end

Status: **later** (excluded for now, user 2026-10-21): needs the cameras, Wi-Fi and the product BL616 firmware.

Every part measured on its own (KPU 0.5 ms, SDIO, NAND, encoder): the time from a wake to the server's answer,
with everything running together.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

To fill: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Wake -> K230 READY (test101) -> capture both cameras -> change net -> history -> POST /v1/check -> answer ->
   (leak) RTMP clip start: timestamps at each step on the K230 console (console_record.py).
2. Same with the server answering "leak" (the clip path) and "no leak" (back to sleep).
3. Battery cost: with test102's current, the energy per wake for each path.

## Pass

Recorded timeline; the longest step named; the leak-to-video time within what the user expects.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| leak -> video steps | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Boot share of the leak-to-video time (test022).

## Battery energy

Energy per leak event, wake to clip uploaded and power-off.

## Obstacles

None yet.
