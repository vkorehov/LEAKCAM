# test030: sleep and minimal currents (every power state's floor)

Status: **HBN + RTC wake work on RC32K** (2026-10-21): sleep 1.23 mA through USB (mostly the charger), wake at 60 s; the Y3 crystal path still fails; battery-lead figure open.

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

## Pass

Sleep current measured and split into the BL616 and the rest; every state's minimum, average and peak recorded; within the ENERGY.txt estimate, or the largest consumer
named with a fix (firmware: HBN level, pins; rev 2: parts).

## Results

### 2026-10-21, through USB (user: no battery-lead cut today), test firmware with `pt_sleep`

Meter in USB-C 1 VBUS (10 A range awake, mA range asleep), cell full (D1 off), K230 rails off, **PR1 still plugged**
(its D+/D- to the host stay connected during the sleep).

| State | Current | Note |
|---|---|---|
| BL616 awake, K230 off | ~60 mA (earlier today: 60.5 mA; 61 mA after the RC32K wake) | USB PHY and console on |
| BL616 in HBN level 0 (`pt_sleep 120`, 23:29:23) | **1.24 mA** | mostly the charger's own input current (ICC <= 1.5 mA with input present); the board's sleep share is not visible through USB |
| PR1 / USB-C 2 unplugged while asleep | no change: 1.24 mA (the USB data lines add nothing) | |
| RTC wake after 120 s | **did not happen**: still 1.24 mA at 23:32+ (awake would be ~60 mA), console not back | see Obstacles; EN tap to recover |

The battery figure (charger <= 6.5 uA IBAT(PDWN) + board) needs the meter in the cell lead: still to do.

### 2026-10-21 evening: the RTC wake traced (test firmware with `pt_rtc`, `pt_sleep <s> [rc]`)

HBN RTC counter rate while awake (`pt_rtc`, 1 s against the BL616 mtimer):

| 32 kHz source | Ticks per s | Note |
|---|---|---|
| RC32K (the default after reset) | 32882-32911 (normalised ~32913) | runs, **+0.44 %** |
| Y3 right after HBN_Power_On_Xtal_32K + HBN_32K_Sel(XTAL) | **0 for the first second**, then 31893 | **the counter stops** until the crystal oscillates |
| Y3 settled | 32754-32758 (normalised ~32764) | runs, **-110 ppm** (~9.5 s/day slow: load caps? test031) |

Sleep runs (meter in USB-C 1 VBUS, K230 off):

| Run | RTC clock | Sleep current | At the wake time | Result |
|---|---|---|---|---|
| 23:29 `pt_sleep 120` | Y3 selected 0 ms after power-on | 1.24 mA | no change | **no wake** (counter frozen: crystal not running, never starts in HBN) |
| 23:44 `pt_sleep 30` | Y3 selected only once the counter moved | 3.95-4.11 mA, falling slowly | no change, no console, no USB popup | **no wake / stuck** (not explained; a top-up charge with D1 off cannot be ruled out) |
| 23:48 `pt_sleep 60 rc` | RC32K | **1.23 mA** | **jumped to 61 mA, console back 23:50:00** | **woke normally** |
| 23:50 `pt_sleep 60` | Y3 ("running after 1000 ms") | not read | not observed: device disconnected | open |

So hibernation and the RTC wake work; the crystal path does not yet. The product's rtc_use_crystal()
(leak_wake.c) selects Y3 at once on a cold boot and assumes the RTC keeps running on RC32K until the crystal
starts: measured wrong, the counter stops. Until the Y3 path is proven, the safe product choice is RC32K for the
HBN wake (+0.44 % = 2.6 s per 10-min wake) with the wall clock corrected against the 40 MHz crystal while awake.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - battery lead (cell + wire), uA/mA range, USB out; voltage - BAT C11.1 at rest and on wake.

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

## Boot time

The BL616's HBN wake to its first instruction and to the console (RTC wake after <s>): part of every humidity slice
and every K230 session start (with test020).

## Battery energy

This test is the sleep figure and every state's floor: uA -> mAh per month (uA x 0.72), power (P = I x VBAT) per
state; the sleep goes into ENERGY.txt section 2 and the budget line "Sleep", the floors into the session lines.

## Obstacles

- **No RTC wake from HBN level 0 with the RTC on Y3** (2026-10-21) -> selecting the 32 kHz crystal before it
  oscillates stops the HBN RTC counter (measured: 0 ticks for ~1 s), and the BL616 never reaches the wake
  compare -> `pt_sleep` now selects Y3 only once the counter moves (Y3 needs ~1 s); on RC32K the wake works
  (60 s, 1.23 mA asleep). With Y3 selected that way the 23:44 run still did not wake (4 mA): open.
- **`pt_rtc` printed empty numbers** -> the SDK's printf has no `%llu` -> 32-bit `%lu` (the deltas are small).
- **ISP flash failed once** ("Could not configure port: Input/output error", LOAD HELP BIN FAIL, 20:39) -> the
  helper load runs before any write, the flash was untouched -> retried, passed.
- **No RTC wake on the first try was recovered with EN** (twice); EN resets the HBN domain too.
