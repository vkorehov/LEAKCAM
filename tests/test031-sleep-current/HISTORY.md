# test031: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

Humidity slices are gone (no humidity wakes; the AHT20 is reported only). The old line (Boot time):

    The BL616's HBN wake to its first instruction and to the console (RTC wake after <s>): part of every humidity slice
    and every K230 session start (with test021).

## Runs

### 2026-10-25: what each wake source and HBN setting costs (same wiring; `pt_sleep 600 <variant>`, own HBN entry)

`pt_sleep` got the SDK's pm_hbn_mode_enter() as its own function (`pt_hbn_enter`) with the build-time choices as
options: `noacomp` (both comparators off, RTC wake only), `norc` (RC32K off while Y3 runs the RTC), `l1` (HBN level
1), `ldo=<mV>` (HBN LDO, SDK 1100). Each variant read on the uA range after ~30 s with every cable out, cell ~4.17 V.

| # | Variant | Battery current | Step |
|---|---|---|---|
| 1 | level 0, leak + USB comparators, RC32K on (the product's sleep) | 35 uA | - |
| 2 | `noacomp`: RTC only | 29 uA | comparators **-6 uA** (~3 uA each) |
| 3 | + `norc` | 29 uA | RC32K 0 |
| 4 | + `l1` | 26 uA | level 1 -3 uA, **but HBN RAM is lost** |
| 5 | + `ldo=900` | 26 uA (24-27 later readings) | LDO 0 |

- **Level 1 loses HBN RAM**: 5 s sleeps, `pt_time` after each: level 0 kept pt_sleep's mark (wake -> main 17 ms),
  level 1 (also at 900 mV) did not. The RTC still wakes it (~7 s). The product keeps its clock, flags and schedule
  there, so level 1 is out (state in flash would mean a flash write per wake).
- 0.80 / 0.70 V not tried: 0.90 V gave nothing, a lower LDO only risks the wake.
- **Floors:** RTC alone ~26 uA = the board's hardware (U5 11 + its divider 5.5, BQ24072T 4.3, probe divider 1.65, the
  rest ~2); BL616 HBN only a few uA. Usable in the product (level 0, leak comparator armed): ~32 uA; +3 uA with the
  USB-plug comparator (kept: without it BLE pairing would start up to one RTC slice after plugging in, for ~2 mAh/
  month).
- Rev 2 lever: a nano-amp regulator for 3V3_SLEEP (about -16 uA).

### 2026-10-25: the battery floor (meter in the cell's + lead, soldered; test firmware `pt_sleep 3600 nousb`)

`nousb` (new): the USB wake is disarmed (also an earlier sleep's arming, kept in the AON registers), so every cable
can come out while it sleeps. Sent on USB power, then USB-C 1, USB-C 2 and PR1 unplugged, meter to the uA range.

| State | Battery current |
|---|---|
| HBN level 0, K230 off, probes dry, Y3 RTC, USB still plugged | 36 uA |
| Same, **every USB cable out** | **36 uA** |
| A USB cable plugged in again (asleep) | 19 uA: part of the board then runs from USB, not the floor; unplugged -> 36 uA again |

Cell at **4.17 V** (just charged): 36 uA x 4.17 V = **150 uW**. The regulator's share depends on the cell voltage;
repeat near 3.6 V (half charge) for the life estimate.

Pull audit (same day, schematic + SDK): HBN powers the core domain off, so every non-AON pad is high-Z with no pull;
every external pull sits at its own rail (R19, R76, R35, R23, R73/R74; the 3V3-switched pull-ups at 0 V): no pin
costs current. Parts: U5 TPS63802 11 uA + its divider 5.5 uA, BQ24072T 4.3 uA typ (6.5 max), BL616 HBN0 ~3-6 uA, probe
divider 1.65 uA, the rest ~2 uA = 26-29 uA typ, 35 max; measured 36 uA. Rev 2 lever: a nano-amp regulator for 3V3_SLEEP
(~-16 uA). Battery: 0.86 mAh/day, ~26 mAh/month (ENERGY.txt).

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

### 2026-10-21 evening: the RTC wake traced (test firmware with `pt_rtc`, `pt_sleep <s> [rc]`)

HBN RTC counter rate while awake (`pt_rtc`, 1 s against the BL616 mtimer):

| 32 kHz source | Ticks per s | Note |
|---|---|---|
| RC32K (the default after reset) | 32882-32911 (normalised ~32913) | runs, **+0.44 %** |
| Y3 right after HBN_Power_On_Xtal_32K + HBN_32K_Sel(XTAL) | **0 for the first second**, then 31893 | **the counter stops** until the crystal oscillates |
| Y3 settled | 32754-32758 (normalised ~32764) | runs, **-110 ppm** (~9.5 s/day slow: load caps? test032) |

Sleep runs (meter in USB-C 1 VBUS, K230 off):

| Run | RTC clock | Sleep current | At the wake time | Result |
|---|---|---|---|---|
| 23:29 `pt_sleep 120` | Y3 selected 0 ms after power-on | 1.24 mA | no change | **no wake** (counter frozen: crystal not running, never starts in HBN) |
| 23:44 `pt_sleep 30` | Y3 selected only once the counter moved | 3.95-4.11 mA, falling slowly | no change, no console, no USB popup | **no wake / stuck** (not explained; a top-up charge with D1 off cannot be ruled out) |
| 23:48 `pt_sleep 60 rc` | RC32K | **1.23 mA** | **jumped to 61 mA, console back 23:50:00** | **woke normally** |
| 23:50 `pt_sleep 60` | Y3 ("running after 1000 ms") | not read | **no valid result**: the board was disconnected overnight | - |

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

### Superseded statements

- Status: **RTC wake fixed** (2026-10-22): HBN on Y3 wakes (5 of 5, cold start included); sleep 1.23 mA through USB (mostly the charger); **on the battery 35-36 uA** as the product sleeps, **26 uA** with the RTC alone (2026-10-25). **Done: not to be repeated** (the battery-lead wiring is too involved; user).
- Setup (2026-10-21): BL616 test firmware with `pt_sleep` (to add: HBN level 0, RTC wake, the K230 side pins parked, AHT20 idle).

## Obstacles

(2026-10-25 first; the older bullets are from 2026-10-21/22.)

- Obstacle (2026-10-25): the first `pt_hbn_enter` build hung awake (52 mA): `static` with one caller, GCC inlined it into
  cmd_sleep, which runs from flash, and it powered the flash down under itself -> `noinline` (it sits in TCM).
- Obstacle (2026-10-25): my check `pt_sleep` without arguments slept 60 s (its default) instead of printing the usage.
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
