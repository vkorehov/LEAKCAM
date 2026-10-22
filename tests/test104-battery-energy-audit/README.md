# test104: battery energy audit

Status: **planned** (2026-10-21): after the product BL616 firmware runs (hibernate, real sessions).

Adds up the per-part figures that every low-level test records (section "Battery energy" in each) in
[ENERGY.txt](../../ENERGY.txt), and checks the sum against a multi-day run. It finds no bottleneck itself: those
show in the low-level tests (user, 2026-10-21).

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

To fill: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Sleep current: from test030 (low level, BL616 HBN with the test firmware); here only checked with the product
   firmware's real sleep.
2. Energy per humidity slice: the current over the ~85 ms wake, integrated (scope over a shunt).
3. Energy per K230 report session: current over the whole session (test101 timing, test102 current), boot part
   separate (test021).
4. Energy per leak session with a clip upload (Wi-Fi).
5. Budget: sleep + 144 slices + 4 reports per day (+ leaks), self-discharge; battery life for 1 and 2 cells.
6. Long run: a charged cell, the real schedule, BAT logged every report session (the K230's ADC, test022) for days;
   compare the slope with the budget.

## Pass

ENERGY.txt filled with measured figures; the budget and the long run agree within ~20 %; battery life recorded.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - battery lead over whole sessions; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |

## Boot time

The boot's share of the daily energy (test021 x wakes per day).

## Battery energy

This test is the audit: ENERGY.txt filled, battery life.

## Obstacles

None yet.
