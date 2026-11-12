# test032: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

Humidity wakes every 10 min (144 a day), NTP for the wall clock and the 24 h server interval on battery are gone: the
RTC wakes the full firmware for the check-in (the server's `checkin`, default 3600 s) and the leak check (`leakcheck`,
default 86400 s), and the clock comes from the server's `X-Time`. The old lines:

    The BL616's hibernate RTC (Y3, 32.768 kHz) wakes it every 10 min for humidity and at the K230's requested interval;
    it also keeps the wall clock between NTP syncs (aon_state.c: ~20 ppm, 1.7 s/day expected). A wake that comes late,
    early or never is a missed report or a missed humidity alarm.

    4. Drift over 24 h without NTP: seconds per day against the 1.7 s/day budget.

    | Per day | 144 RTC wakes a day for humidity (every 10 min, DESIGN 5.1), inside the test031 sleep budget; the server check once a day on battery (24 h interval, DESIGN.md 5.3.1) wakes on its own | ENERGY.txt line | n per day |

    HBN wake to the first instruction and to a usable console (part of every humidity slice and session start).

    Energy per RTC wake with nothing to do (the humidity slice without the K230): current over the awake time, x 144 per
    day (ENERGY.txt section 3).

## Runs

### Automated run 2026-11-11 (run.py, 1301 s): pass

RTC wakes: 10 s -> 14.67, 13.72, 13.74 s; 600 s -> 603.79, 603.76 s (host, command -> console back).


### Automated run 2026-11-01 (run.py, 1285 s): pass

RTC wakes: 10 s -> 14.56, 13.64, 13.65 s; 600 s -> 603.67, 603.71 s (host, command -> console back).


### Automated run 2026-10-25 (run.py, 1285 s): pass

RTC wakes: 10 s -> 14.60, 13.68, 13.69 s; 600 s -> 603.69, 603.71 s (host, command -> console back).

The run.py of this run averaged the lengths and printed -499 ppm: the first 10 s wake took 0.9 s longer (USB
enumeration) and moved the mean. From the shortest of each (enumeration only adds): 603.69 - 13.68 = 590.01 s for 590 s
= **+17 ppm** (+-35 ppm: 20 ms polling at both ends); within the +-20 ppm target, not yet proven inside it. run.py
now takes the shortest.

### Automated run 2026-10-25 (run.py, 454 s): pass

RTC wakes: 10 s -> 13.69, 13.67, 13.67 s; 120 s -> 123.69, 123.68, 123.68 s (host, command -> console back).

The +75 ppm is **not conclusive**: the host polls the console in 20 ms steps, so over the 110 s difference the
error band is about +-100 ppm (2026-10-22 gave -110 ppm). run.py now sleeps 600 s for the long case (+-20 ppm).

Old status line (2026-10-22): **wake works** (2026-10-22, test031 fix): HBN RTC wake on Y3 5 of 5; Y3 -110 ppm (target +-20) open.

### Superseded metric values

- Test run time (2026-10-25, first automated run): "454 s automated (run.py, 2026-10-25), no setup".

## Obstacles

- **No HBN RTC wake on Y3** -> fixed (test031): HBN pin pulls off + wait for Y3 before the sleep. Still open: Y3
  measured **-110 ppm** while awake (target +-20 ppm): check its load capacitors against the crystal's CL.
