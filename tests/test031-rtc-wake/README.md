# test031: RTC wake (BL616 HBN timer, 32.768 kHz crystal)

Status: **planned** (2026-10-21).

The BL616's hibernate RTC (Y3, 32.768 kHz) wakes it every 10 min for humidity and at the K230's requested interval;
it also keeps the wall clock between NTP syncs (aon_state.c: ~20 ppm, 1.7 s/day expected). A wake that comes late,
early or never is a missed report or a missed humidity alarm.

## Steps

1. Test firmware `pt_sleep <s>` (test030): RTC wake after 10, 60, 600 s; the wake time against the host clock
   (console reappears) and the BL616's own RTC count.
2. 100 cycles of 60 s: every wake arrives, none missed, spread of the error.
3. Crystal: Y3 frequency (scope on IO17, or the RTC count against the host over 1 h): ppm.
4. Drift over 24 h without NTP: seconds per day against the 1.7 s/day budget.
5. The K230 PMU RTC: test036.

## Pass

Every wake within +-1 s at 600 s, none missed in 100; crystal within +-20 ppm; drift <= 2 s/day.

## Boot time

HBN wake to the first instruction and to a usable console (part of every humidity slice and session start).

## Battery energy

Energy per RTC wake with nothing to do (the humidity slice without the K230): current over the awake time, x 144 per
day (ENERGY.txt section 3).
