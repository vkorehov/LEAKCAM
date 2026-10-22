# test031: RTC wake (BL616 HBN timer, 32.768 kHz crystal)

Status: **wake works** (2026-10-22, test030 fix): HBN RTC wake on Y3 5 of 5; Y3 -110 ppm (target +-20) open.

The BL616's hibernate RTC (Y3, 32.768 kHz) wakes it every 10 min for humidity and at the K230's requested interval;
it also keeps the wall clock between NTP syncs (aon_state.c: ~20 ppm, 1.7 s/day expected). A wake that comes late,
early or never is a missed report or a missed humidity alarm.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Steps

1. Test firmware `pt_sleep <s>` (test030): RTC wake after 10, 60, 600 s; the wake time against the host clock
   (console reappears) and the BL616's own RTC count.
2. 100 cycles of 60 s: every wake arrives, none missed, spread of the error.
3. Crystal: Y3 frequency (scope on IO17, or the RTC count against the host over 1 h): ppm.
4. Drift over 24 h without NTP: seconds per day against the 1.7 s/day budget.
5. The K230 PMU RTC: test036.

## Pass

Every wake within +-1 s at 600 s, none missed in 100; crystal within +-20 ppm; drift <= 2 s/day.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - battery lead, USB out (sleep/wake); voltage - 3V3_SLEEP, VBAT C15.1 (BL616 only).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| wake time error, crystal ppm | not measured yet | +-20 ppm | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

HBN wake to the first instruction and to a usable console (part of every humidity slice and session start).

## Battery energy

Energy per RTC wake with nothing to do (the humidity slice without the K230): current over the awake time, x 144 per
day (ENERGY.txt section 3).

## Obstacles

- **No HBN RTC wake on Y3** -> fixed (test030): HBN pin pulls off + wait for Y3 before the sleep. Still open: Y3
  measured **-110 ppm** while awake (target +-20 ppm): check its load capacitors against the crystal's CL.
