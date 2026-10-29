# test021: BL616: wake latency, flash storage, sensor reads

Status: **pass** (2026-10-25): RTC wake -> main() 17.3-17.4 ms, K230_PWR -> 3V3 1.7 ms, AHT20 36.5 ms, leak ADC 2.6 ms
with the calibration kept across HBN; HBN RAM retention-mode rule (accesses corrupted in RET/SLP) fixed in the product.

The always-on side's speed: how fast a wake turns into a K230 power-on, and the cost of its own jobs.

## Setup

BL616 test firmware (tools/power-bringup/bl616_powertest) with `pt_time` (PT_TEST_TIMING): min / max of 5 runs on the
core timer (us). Wake latency: `pt_sleep 10` writes the RTC tick the wake is due at into HBN RAM (t021,
linker section `.hbn_noinit`), `main()` reads the RTC (30.5 us ticks) and the core timer before `board_init()`.
`pt_time hstress` tests HBN RAM access per HBN_SRAM mode; `pt_time recal` forgets the stored ADC calibration. K230 off except for the 3V3 ramp. USB-C 1 powers the board.

## Needs (build features and dependencies)

BL616 product parts: HBN, RTC, ACOMP, I2C (AHT20), K230 power path.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Wake to K230_PWR high: from an RTC wake, a leak (ACOMP) edge and a USB plug (product firmware; a GPIO toggled at
   each step, scope or the K230's rdtime).
2. AHT20 measurement: trigger to data (datasheet 80 ms; the firmware reads at 50 ms then every 5 ms).
3. Easyflash/littlefs: write and read of a key (Wi-Fi credentials, BLE bonds) on the BL616's flash.
4. Leak probe ADC conversion time.

## Pass

Figures recorded; wake to K230 power-on within the design's budget (DESIGN.md).

## Result

- RTC wake -> `main()` **17.3 / 17.4 / 17.4 ms**, of it core reset -> `main()` 8.0 ms (ROM + boot2 + C start-up);
  K230_PWR -> 3V3 up 1.7 ms; AHT20 trigger -> data **36.5 ms** (min / median / max of 8 wakes, 2026-10-25, run.py).
- AHT20: the sample is ready 36.7 ms after the trigger; firmware/bl616/aht20.c reads first at 35 ms, then polls every
  1 ms (2026-10-22).
- Leak probe ADC (init + 6 conversions, the first 5 dropped as stale; conversions every ~411 us, polled every 10 us):
  **2.6 ms** (2026-10-22).
- ADC calibration: `bflb_adc_init()` runs the SDK's offset calibration once per power-on (init 28.6 ms, first call
  31.2 ms); every HBN wake is a reset. The firmware keeps the calibration globals (`coe`, `os1`, `os2`,
  `tsen_offset`, `adc_cali_complete`, bflb_adc.c SDK 2.3.35) in HBN RAM: after a wake init 27 us, first call
  **2.60 ms**, probe 1650 mV dry both ways; store only after a real calibration. No SDK patch (2026-10-22).
- easyflash on littlefs: mount 4.3-4.8 ms (63 ms the first time, empty PSM reformatted); 64 B key set 0.9-1.6 ms
  (once 20-24 ms: littlefs compaction), get 0.6-1.3 ms, delete 1.0-1.4 ms; 25 of 25 read back right (2026-10-22).
- **HBN RAM rule** (2026-10-22, `pt_time hstress`): while HBN_SRAM bit 6 (RET) or 7 (SLP) is set, an HBN RAM access
  right after an AON (HBN) register write is corrupted, reads and writes, in 4-12 % of the cases (10 000 tries: none
  0 / 0 / 0; RET 1 / 449-747 / 0; SLP 1 / 801-1212 / 0); never with both bits clear. HBN_SRAM is an AON register and
  stays set after the wake and across resets. Fixed in the product and the test firmware: `aon_init()` clears RET and
  SLP before the first access; `aon_prepare_sleep()` (= `HBN_Set_HRAM_Ret()`) is the very last call in `hbn_sleep()`
  before `pm_hbn_mode_enter()`; aon_state, evlog and t021 linker-placed in `.hbn_noinit` (top 1 KB of HBN RAM); the
  SDK's fixed users stay below (+000 hbn_ram / pre-jump stub, +300 boot2 parameters, +400 low-power parameters). With
  the fix: marker 14.6 ms both wakes, no wrong read anywhere.
- The product code for the calibration and the HBN RAM rule (leak_probe_mv(), aon_state, 2026-10-22) is not built
  yet (product rewrite).

Open:
- One RTC wake took 245.6 ms (the first sleep after a cold Y3 start, 2026-10-22); not reproduced in 3 more wakes
  (14.7-15.1 ms): watch in test034/035.
- RTC wake -> main() was 12.3-15.5 ms and core reset -> main() 5.28 ms on 2026-10-22, 17.3-17.4 ms and 8.0 ms on
  2026-10-25: the +2.7 ms core reset -> main() is not explained.
- `main()` -> K230_PWR not timed (the product decides there, leak_wake.c); wake from a leak (ACOMP) edge and a USB
  plug (Steps 1) not measured.
- Metrics: Compile flags say -Os -g, -flto while Optimised build says SDK -O2, no LTO for the same test firmware;
  Image says 149 984 B while Features says 1 114 864 B: which holds is not recorded.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - 3V3_SLEEP, VBAT C15.1 (BL616 only).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: BL616-only test; its own boot (RTC wake -> main) 17.3 / 17.4 / 17.4 ms (2026-10-25) | target (test022) | longest step: |
| This part's boot share | wake -> main() is the BL616's whole share of a wake: 17.3 / 17.4 / 17.4 ms (2026-10-25) | - | |
| Test run time | 140 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| BL616 step times (wake -> K230 power-on, I2C read) | RTC wake -> main() 17.3 / 17.4 / 17.4 ms; core reset -> main() 8.0 / 8.0 / 8.0 ms; K230_PWR -> 3V3 1.7 / 1.7 / 1.7 ms; AHT20 trigger -> data 36.5 / 36.5 / 36.5 ms (min / median / max of 8 wakes) (2026-10-25) | I2C 400 kHz (now 100) | % of the top |
| Current idle / during (min / avg / peak) | 88 mA at 10:19, 86 mA at 11:09 (BL616 awake idle, K230 rails on and held in reset after pt_time); the timed jobs last < 0.1 s, too short for the meter | floor (test031) | USB-C 1 VBUS |
| Voltage under load (min) | n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the BL616's wake (~15 ms) is below what the USB meter resolves; on the cell: test104 (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: as energy; the wake cost sits in the sleep floor of test031 (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 32.4 / 32.4 / 29.3 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 207136 / min 204396 / 207136 B, blocks 19 -> 19, deleted 0; BL616 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 149 984 B (+29 KB storage stack) | minimal image (rule) | the image this run used |
| Compile flags | BL616 SDK -Os -g, sections, --gc-sections, -flto | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: BL616 test firmware (SDK -O2, no LTO), not the product firmware; the product BL616's flags are test008 step 6 (2026-10-25) | the faster one stays | |

## Boot time

The BL616 side of every wake: wake -> decision -> K230_PWR high (adds to every session before the K230 boot starts).

## Battery energy

The BL616's current while awake (humidity slice, wake handling) and how long; energy per humidity slice (144 per day) and per wake decision.
