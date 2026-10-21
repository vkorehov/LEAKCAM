# test104: battery energy audit

Status: **planned** (2026-10-21): after the product BL616 firmware runs (hibernate, real sessions).

Adds up the per-part figures that every low-level test records (section "Battery energy" in each) in
[ENERGY.txt](../../ENERGY.txt), and checks the sum against a multi-day run. It finds no bottleneck itself: those
show in the low-level tests (user, 2026-10-21).

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


## Boot time

The boot's share of the daily energy (test021 x wakes per day).

## Battery energy

This test is the audit: ENERGY.txt filled, battery life.
