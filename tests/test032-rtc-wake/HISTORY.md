# test032: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

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
