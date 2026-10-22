# test030: sleep and minimal currents (every power state's floor)

Status: **RTC wake fixed** (2026-10-22): HBN on Y3 wakes (5 of 5, cold start included); sleep 1.23 mA through USB (mostly the charger); battery-lead figure open.

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
| 23:50 `pt_sleep 60` | Y3 ("running after 1000 ms") | not read | **no valid result**: the board was disconnected overnight | - |

### 2026-10-22: the cause and the fix

Two things stop the Y3 RTC in HBN; both are needed:
1. **HBN pin pulls on GPIO16-19.** In HBN the chip puts hardware pulls on its HBN pins unless disabled; IO16/IO17
   are the Y3 crystal, the pulls stop the oscillator, the counter freezes, no wake. `pm_hbn_mode_enter()` does not
   disable them (the SDK's `HBN_Mode_Enter()` does: `HBN_Hw_Pu_Pd_Cfg(DISABLE)`, "0x4000F014[16]=0"). Fix: call it
   before the sleep.
2. **Y3 must already run.** Selected while starting (~1 s after a cold power-on; the SDK's HBN_Power_On_Xtal_32K
   waits 1.1 ms despite its ">1s" comment) the counter stops, and the crystal does not start in HBN. Fix: select
   Y3, wait until the counter moves (<= 3 s), else RC32K so the chip still wakes.

| Run (test firmware) | Y3 ready | Wake |
|---|---|---|
| 07:42 `pt_sleep 60`, pulls off, wait | after 1000 ms | console back after 68 s |
| 07:43 `pt_sleep 30` x 2 | after 0 ms (still running from the last wake) | 37 s, 36 s |
| 07:51 `pt_sleep 60` (charge done) | 0 ms | 66 s; **1.23 mA asleep, 61 mA awake after** |
| 07:53 cold start, pulls off, **no wait** | - | **no wake** (the wait is needed) |
| 07:57 cold start, pulls off, wait (final code) | after 1030 ms | **36 s** |

The polling-and-switching workaround and `pt_sleep ... rc` of 2026-10-21 are removed again; the final code is
`rtc_on_xtal()` (wait) + `HBN_Hw_Pu_Pd_Cfg(DISABLE)` in bl616_powertest, the same in the product (leak_wake.c
rtc_use_crystal(), hbn_sleep(); not run yet: the product image is rebuilt at the end).

So hibernation and the RTC wake work on RC32K; **on Y3 the wake never comes, even with the crystal running before the
sleep** (the counter runs on Y3 while awake: the oscillator or its clock path apparently stops in HBN). The product's rtc_use_crystal()
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
| Image (sizes) | BL616 test firmware 121 KB (+ boot2 53 KB) | minimal image (rule) | the image this run used |
| Compile flags | BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | smallest code (rule) | |

## Boot time

The BL616's HBN wake to its first instruction and to the console (RTC wake after <s>): part of every humidity slice
and every K230 session start (with test020).

## Battery energy

This test is the sleep figure and every state's floor: uA -> mAh per month (uA x 0.72), power (P = I x VBAT) per
state; the sleep goes into ENERGY.txt section 2 and the budget line "Sleep", the floors into the session lines.

## Obstacles

- **No RTC wake from HBN on Y3** (2026-10-21/22) -> the HBN pin pulls on GPIO16-19 stop the Y3 oscillator in HBN
  (pm_hbn_mode_enter does not disable them), and Y3 selected before it runs stops the counter and does not start in
  HBN -> `HBN_Hw_Pu_Pd_Cfg(DISABLE)` before the sleep + wait until Y3 runs (<= 3 s, else RC32K). 5 of 5 wakes incl.
  a cold start; the product has the same fix.
- **"Asleep overnight" was not a valid result** -> the board had been disconnected overnight (user) -> the Y3
  result taken again the next morning.
- **`pt_rtc` printed empty numbers** -> the SDK's printf has no `%llu` -> 32-bit `%lu`.
- **ISP flash failed once** ("Could not configure port: Input/output error", LOAD HELP BIN FAIL) -> nothing written
  (fails while loading the helper) -> retried, passed.
- **Builds took very long** -> three stale `make -j5` containers (one 20 h old: the abandoned bisect build in
  tmp_bisect) -> stopped by container ID; tmp_bisect and the product build dir deleted (product image rebuilt at
  the end, user).
