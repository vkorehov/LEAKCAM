# test030: sleep and minimal currents (every power state's floor)

Status: **planned** (2026-10-21).

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
- BL616 test firmware with `pt_sleep` (to add: HBN level 0, RTC wake, the K230 side pins parked, AHT20 idle).

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
| Agent waiting on the link (READY sent) | the agent running, BL616 not answering | the floor a session sits at while it waits |
| BL616 awake, idle (test firmware prompt) | K230 rails off | the BL616's own awake floor |

Each state: minimum, average and peak over >= 10 s (meter min/max hold, or a shunt and scope for the peaks); the
range that reads it (uA / mA / A) and the meter's resolution there.

## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

The BL616's HBN wake to its first instruction and to the console (RTC wake after <s>): part of every humidity slice
and every K230 session start (with test020).

## Battery energy

This test is the sleep figure and every state's floor: uA -> mAh per month (uA x 0.72), power (P = I x VBAT) per
state; the sleep goes into ENERGY.txt section 2 and the budget line "Sleep", the floors into the session lines.

## Pass

Sleep current measured and split into the BL616 and the rest; every state's minimum, average and peak recorded; within the ENERGY.txt estimate, or the largest consumer
named with a fix (firmware: HBN level, pins; rev 2: parts).
