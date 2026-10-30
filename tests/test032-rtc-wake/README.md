# test032: RTC wake (BL616 HBN timer, 32.768 kHz crystal)

Status: **partial** (2026-10-25): HBN RTC wake on Y3 works (10 s and 600 s, every wake arrived); Y3 +17 ppm with a
+-35 ppm band (target +-20); 100-cycle run and 24 h drift not run.

The BL616's hibernate RTC (Y3, 32.768 kHz) wakes it every 10 min for humidity and at the K230's requested interval;
it also keeps the wall clock between NTP syncs (aon_state.c: ~20 ppm, 1.7 s/day expected). A wake that comes late,
early or never is a missed report or a missed humidity alarm.

## Setup

BL616 test firmware `bl616_powertest` (`pt_sleep <s>`, test031), K230 off; wake timed by the host (command ->
console back). Probes: Metrics. Automated: run.py.

## Needs (build features and dependencies)

BL616 HBN RTC, XTAL32K (Y3), HBN pin config (HBN_Hw_Pu_Pd_Cfg).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Test firmware `pt_sleep <s>` (test031): RTC wake after 10, 60, 600 s; the wake time against the host clock
   (console reappears) and the BL616's own RTC count.
2. 100 cycles of 60 s: every wake arrives, none missed, spread of the error.
3. Crystal: Y3 frequency (scope on IO17, or the RTC count against the host over 1 h): ppm.
4. Drift over 24 h without NTP: seconds per day against the 1.7 s/day budget.
5. The K230 PMU RTC: test037.

## Pass

Every wake within +-1 s at 600 s, none missed in 100; crystal within +-20 ppm; drift <= 2 s/day.

## Result

- HBN RTC wake on Y3 works: 10 s -> 14.60, 13.68, 13.69 s; 600 s -> 603.69, 603.71 s (host, command -> console back,
  incl. USB enumeration; spread 0.02 s) (2026-10-25, run.py 1285 s run). Needs HBN pin pulls off + wait for Y3 before
  the sleep (test031, 2026-10-22).
- Y3 **+17 ppm** (+-35 ppm band: 20 ms host polling at both ends), from the shortest of each: 603.69 - 13.68 =
  590.01 s for 590 s (2026-10-25). Within +-20 ppm, not yet proven inside it.
- Open: crystal within +-20 ppm not proven (scope on IO17 or 1 h count, step 3); if it is off, check Y3's load
  capacitors against the crystal's CL.
- Open: 100 cycles of 60 s (step 2) and 24 h drift (step 4) not run.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - battery lead, USB out (sleep/wake); voltage - 3V3_SLEEP, VBAT C15.1 (BL616 only).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: BL616-only; RTC wake -> main() 12-16 ms (test021) (2026-10-25) | target (test022) | longest step: |
| This part's boot share | n/a: BL616-only (2026-10-25) | - | |
| Test run time | 1285 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| wake time error, crystal ppm | Y3 +17 ppm, +-35 ppm band (host-timed, shortest of 3 x 10 s vs of 2 x 600 s; console return 3.99 s after the due time incl. USB enumeration; spread 0.02 s) (2026-10-25) | +-20 ppm | % of the top |
| Current idle / during (min / avg / peak) | BL616 awake on USB, rails off 0.061 A (2026-10-25); HBN sleep on the cell 35 uA (test031) (2026-10-25, from recorded readings) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the sleep itself is test031 (35 uA); the wake is below the USB meter's resolution (2026-10-25) | - | scripts/energy.py |
| Per day | 144 RTC wakes a day for humidity (every 10 min, DESIGN 5.1), inside the test031 sleep budget; the server check every 15 min (96 a day, test100) wakes on its own | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 32.2 / 32.2 / 23.7 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; leak: n/a, 6 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 121 KB (+ boot2 53 KB) | minimal image (rule) | the image this run used |
| Compile flags | BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

HBN wake to the first instruction and to a usable console (part of every humidity slice and session start).

## Battery energy

Energy per RTC wake with nothing to do (the humidity slice without the K230): current over the awake time, x 144 per
day (ENERGY.txt section 3).
