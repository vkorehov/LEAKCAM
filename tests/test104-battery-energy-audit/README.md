# test104: battery energy audit

Status: **planned** (2026-10-21): after the product BL616 firmware runs (hibernate, real sessions).

Fills [ENERGY.txt](../../ENERGY.txt): every consumer and every kind of wake measured, the budget per day and month,
and the expected battery life with one and with two cells.

## Steps

1. Sleep current: board on battery only, BL616 hibernating (product firmware), meter in the battery lead on a
   uA range (with a burden-free meter or a shunt and averaging: the humidity slices are short peaks).
2. Energy per humidity slice: the current over the ~85 ms wake, integrated (scope over a shunt).
3. Energy per K230 report session: current over the whole session (test101 timing, test102 current), boot part
   separate (test021).
4. Energy per leak session with a clip upload (Wi-Fi).
5. Budget: sleep + 144 slices + 4 reports per day (+ leaks), self-discharge; battery life for 1 and 2 cells.
6. Long run: a charged cell, the real schedule, BAT logged every report session (the K230's ADC, test022) for days;
   compare the slope with the budget.

## Pass

ENERGY.txt filled with measured figures; the budget and the long run agree within ~20 %; battery life recorded.
