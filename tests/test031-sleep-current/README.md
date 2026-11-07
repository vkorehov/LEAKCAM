# test031: sleep and minimal currents (every power state's floor)

Status: **pass, done** (2026-10-25): on the battery the product sleep (HBN level 0, comparators armed) draws 35 uA,
29 uA with the RTC alone; HBN wake on the Y3 RTC works (2026-10-22). Not to be repeated (the battery-lead wiring is
too involved; user).

The lowest current each power state can reach, measured on its own, so every other test's figure reads as a rise
above a known floor (user, 2026-10-21: minimal currents too). First the sleep: what the board draws between wakes,
which is most of its life. ENERGY.txt estimates ~15-20 uA (BL616 HBN "a few
uA", U5 TPS63802 ~11 uA, probe divider 1.65 uA, AHT20, charger); this test measures it, part by part. It needs no
product firmware: a `pt_sleep <s>` command in the test firmware enters the same hibernate the product uses (HBN level
0, `pm_hbn_mode_enter`) with an RTC wake after <s> seconds.

## Setup

- Cell charged; **USB-C 1 and USB-C 2 unplugged**; PR1 unplugged once the command is sent (its USB lines would keep
  the BL616's USB PHY and the host's pull-ups in the circuit).
- Meter in series in the cell's + lead (cell + -> meter mA/uA jack, COM -> JB1 pin 1), **uA range** for sleep, mA for
  the awake states; mind the burden: switch to the mA/A input before the BL616 wakes (or give pt_sleep long enough).
- BL616 test firmware with `pt_sleep <s> [nousb | noacomp | norc | l1 | ldo=<mV>]` (HBN level 0, RTC wake; own
  HBN entry `pt_hbn_enter`), `pt_rtc`, `pt_time`.

## Needs (build features and dependencies)

BL616: HBN/PM (bl616_pm.c, pm_hbn_mode_enter), RTC (HBN counter, Y3), PT_TEST_SLEEP; USB CDC console only for setup.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

| State | How | Expected |
|---|---|---|
| BL616 awake, idle, K230 rails off | test firmware at its prompt | a few mA |
| BL616 in HBN level 0 | `pt_sleep 120`, unplug PR1, read after ~10 s | the board's sleep current |
| BL616 held in reset | PR1 pad 5 (EN) to GND | the rest of the board: U5, divider, AHT20, charger |
| Leak probes wet | S1 shorted while asleep | + the divider's wet share; an ACOMP wake in the product |
| After the RTC wake | console back after <s> | wake works, time to console |

Then: BL616 HBN share = (HBN) - (EN low); compare each part with ENERGY.txt section 2.

### Minimal currents with the K230 on (the session floor)

| State | How | Measures |
|---|---|---|
| K230 rails on, held in reset | `pt_on` | the rails' own floor (regulators, leakage), no K230 activity |
| K230 at the msh prompt, nothing running | `pt_run`, wait for msh | the idle floor of RT-Smart at boot clocks |
| Same, CPU at its lowest profile | power manager: CPU 0.2 GHz (profiles in pm_domain_cpu.c) | what a slower CPU saves while waiting |
| Same, KPU and VPU clock-gated or powered off | power manager domains off | the media blocks' idle share |
| Product program waiting on the link (READY sent) | `/app/leakcam` running, BL616 not answering the READY | the floor a session sits at while it waits |
| BL616 awake, idle (test firmware prompt) | K230 rails off | the BL616's own awake floor |

Each state: minimum, average and peak over >= 10 s (meter min/max hold); the
range that reads it (uA / mA / A) and the meter's resolution there.

## Pass

Sleep current measured and split into the BL616 and the rest; every state's minimum, average and peak recorded; within the ENERGY.txt estimate, or the largest consumer
named with a fix (firmware: HBN level, pins; rev 2: parts).

## Result

- Battery floor, every cable out, cell 4.17 V (`pt_sleep 600 <variant>`, 2026-10-25): product sleep (level 0, leak +
  USB comparators, RC32K on) **35 uA** = 146 uW; RTC only (`noacomp`) 29 uA (comparators ~3 uA each); RC32K off 0;
  HBN LDO 900 mV 0; level 1 -3 uA but loses HBN RAM, so level 0 stays. With a USB cable in, 19 uA (part of the board
  then runs from USB).
- Split (2026-10-25): the board's hardware ~26 uA (U5 TPS63802 11 + its divider 5.5, BQ24072T 4.3, probe divider
  1.65, the rest ~2); BL616 HBN only a few uA. Pull audit: every non-AON pad high-Z in HBN, every external pull at its
  own rail: no pin costs current. Largest consumer U5 + divider: rev 2 lever a nano-amp regulator for 3V3_SLEEP
  (about -16 uA).
- In the product: level 0 with the leak comparator armed ~32 uA, +3 uA with the USB-plug comparator (kept); 0.84
  mAh/day asleep (ENERGY.txt) (2026-10-25).
- RTC wake from HBN on Y3 works with `HBN_Hw_Pu_Pd_Cfg(DISABLE)` before the sleep and a wait until Y3 runs (<= 3 s,
  else RC32K): 5 of 5 incl. a cold start; a cold start without the wait does not wake (2026-10-22). Confirmed at 10 s
  and 600 s in test032 (2026-10-25). Wake -> main 17 ms at level 0 (2026-10-25).
- Clocks while awake: RC32K +0.44 % (2026-10-21); Y3 +17 ppm (test032, 2026-10-25).
- Through USB: asleep 1.23 mA (mostly the charger's own input current), awake idle ~60 mA (2026-10-21/22).

Open:
- Not measured, and not to be (no battery measurements (user, 2026-10-30)): the figure near 3.6 V (half charge; measured at 4.17 V), BL616 held in
  reset, the K230-on floors. Probes wet: ~1.4 uA over the floor, calculated (test034).
- The product's copy of the Y3 fix (leak_wake.c rtc_use_crystal(), hbn_sleep()) not run yet.

## Metrics (skill leakcam-test-metrics)

Probes: current - battery lead (cell + wire), uA/mA range, USB out; voltage - BAT C11.1 at rest and on wake.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: sleep test, the K230 stays off | target (test022) | longest step: |
| This part's boot share | n/a: no K230 boot; BL616 RTC wake -> main 17 ms (pt_time, level 0) | - | |
| Test run time | each variant ~2 min to settle and read; the session ~70 min with the wiring | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | n/a: no rate | - | % of the top |
| Current idle / during (min / avg / peak) | asleep 35 uA (product), 29 uA RTC only, 26 uA level 1; awake idle ~60 mA (2026-10-21, USB) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | cell 4.17 V at rest (just charged); on wake not probed | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 35 uA x 4.17 V = 146 uW asleep | - | scripts/energy.py |
| Per day | 0.84 mAh/day asleep (ENERGY.txt) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | n/a: not read in the sleep runs (board ~31 C earlier, AHT20) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | n/a: no repeated operation; the sleep state lives in HBN RAM | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 121 KB (+ boot2 53 KB) | minimal image (rule) | the image this run used |
| Compile flags | BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | smallest code (rule) | |
| Features over the minimal image | test-only: pt_sleep variants (noacomp, norc, l1, ldo=), pt_hbn_enter; the product keeps the SDK entry | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: sleep current is set by the hardware, not by code speed | the faster one stays | |

## Boot time

The BL616's HBN wake to its first instruction and to the console (RTC wake after <s>): part of every check-in and
every K230 session start (with test021).

## Battery energy

This test is the sleep figure and every state's floor: uA -> mAh per month (uA x 0.72), power (P = I x VBAT) per
state; the sleep goes into ENERGY.txt section 2 and the budget line "Sleep", the floors into the session lines.
