# test021: BL616: wake latency, flash storage, sensor reads

Status: **pass** (2026-10-22): RTC wake -> main() 12-16 ms, 3V3 ramp 1.7 ms, AHT20 50 -> 38 ms, ADC 3.1 -> 2.6 ms, ADC calibration kept across HBN (first reading after a wake 31.2 -> 2.6 ms); found the HBN RAM retention-mode rule (accesses corrupted in RET/SLP mode), fixed in the product.

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

## Results

### 2026-10-22

| Timing | Value | Note |
|---|---|---|
| RTC wake -> `main()` | **12.3, 13.8, 15.2, 15.5 ms**; once 245.6 ms | outlier: the first sleep after a cold Y3 start (Y3 running 1020 ms before), open |
| of which core reset -> `main()` (ROM + boot2 + C start-up) | 5.28 ms (cold reset and wake alike) | the core timer counts from the core's reset |
| so wake -> core reset (power-up, clocks) | ~7-10 ms | |
| `main()` -> K230_PWR | not timed: the product decides here (leak_wake.c) | |
| K230_PWR -> 3V3 up | 1.68-1.70 ms | 4 runs |
| AHT20 trigger -> data | 50.2-51.2 ms (37.9 ms after the change below) | sample ready at 36.7 ms |
| Leak probe ADC (init + 6 conversions, the first 5 dropped) | 3.1 ms (2.6 ms after the change below); 31.8 ms on the first call after a reset | SDK calibration, below |
| easyflash on littlefs: mount | 4.3-4.8 ms; 63 ms the first time (empty PSM, reformatted) | |
| easyflash 64 B key: set / get / delete | 0.9-1.6 ms (once 20-24 ms: littlefs compaction) / 0.6-1.3 ms / 1.0-1.4 ms | 25 of 25 read back right |

### 2026-10-22, open points chased

| Point | Found | Done |
|---|---|---|
| AHT20 50 ms | the sample is ready **36.7 ms** after the trigger (busy bit polled every 1 ms, 5 of 5 at 36.72-36.73 ms); the driver read first at a fixed 50 ms | firmware/bl616/aht20.c: first read at 35 ms, then 1 ms polls: **37.9 ms** (-12.3 ms per reading) |
| ADC 3.1 ms | conversions every ~411 us (16-bit, clock / 32); the loop slept 1 ms whenever the count was 0, so results came in pairs on whole ms | leak_wake.c and the test: poll every 10 us: **2.6 ms** (6 conversions, 5 dropped as stale) |
| ADC one run 31.8 ms | only the **first call after each reset**: `bflb_adc_init()` runs the SDK's offset calibration once per power-on (20 conversions on VBAT/2 + `bflb_mtimer_delay_ms(10)`): init **28.6 ms**. Every HBN wake is a reset, so every leak wake pays it | fixed: the calibration is kept across HBN (below) |
| 245 ms wake | not reproduced: 3 more wakes right after a cold Y3 start (920-1010 ms) took 14.7-15.1 ms | open, watch in test034/035 |
| HBN RAM keeps data? | yes, across HBN level 0 and resets, **but** only when it is not in retention mode while running: see "HBN RAM" below (the first readings here were taken in retention mode and were partly luck) | explained and fixed below |

### 2026-10-22, ADC calibration kept across HBN, and the HBN RAM rule it uncovered

**ADC calibration.** bflb_adc.c (SDK 2.3.35) keeps its calibration result in globals (`coe`, `os1`, `os2`,
`tsen_offset`) and runs the calibration only while `adc_cali_complete` is 0. No SDK patch is needed: the firmware
declares them `extern`, stores the cold boot's result in HBN RAM and hands it back after a wake, setting the flag.
Store only when `bflb_adc_init()` really calibrated (else every reading re-stores it). Final run, test firmware:

| Wake | First ADC call | Probe |
|---|---|---|
| wake 1 (stored calibration cleared with `pt_time recal`) | init 28 597 us, 31.16 ms: calibrated and stored | 1650 mV dry |
| wake 2 | **init 27 us, 2.60 ms**: restored, no calibration | 1650 mV dry |

-28.6 ms on every leak or RTC wake that reads the probe; in the product (leak_probe_mv(), aon_state) since
2026-10-22, not built yet (rewrite).

**HBN RAM rule.** Keeping the calibration in HBN RAM failed in ways that looked random: the wake marker came back 0,
then 0x02000000; the restore check read the wrong flag although main() had read it right; `recal` did not take.
Probes and experiments, in order (each a flash + 2-5 VMware clicks):

| Step | Result |
|---|---|
| marker / calibration at +F80, +F00, +080 (hand-placed, then linker-placed) | lost or wrong, at every location |
| full fill of HBN RAM before the sleep, scanned at main() before and after board_init() | nothing changed but the first word written (+010) |
| t021 dumped before / after board_init() | unchanged; but a probe after pins_init() read 0xC319xxxx, 0.5 ms later the right value again |
| 100 000 reads in a loop | 0 wrong; but the first read of a run was wrong once |
| write in mode none / RET / SLP, then reset, read at main() | the write survives in every mode; the **first** read after the reset was 0x00000000 (RET) and 0x7728C800 (SLP) for a 0x77xxxxxx, right in mode none |
| **`pt_time hstress`**: 10 000 x {write word, read}; {AON register write, read word}; 200 x {write, 1 ms, read} | **none 0 / 0 / 0; RET 1 / 449-747 / 0; SLP 1 / 801-1212 / 0** (two runs, a third in the final build: 611 / 718) |
| fix: RET/SLP off at main() before the first access, RET on only after the last write before HBN | marker 14.6 ms both wakes, restore as above, no wrong read anywhere |

**Cause:** HBN_SRAM bits 6 (RET) and 7 (SLP) select what HBN RAM does in HBN. While either is set, an HBN RAM access
right after an AON (HBN) register write is corrupted, reads and writes, in 4-12 % of the cases; never with both
bits clear. HBN_SRAM is an AON register: whatever the last sleep set stays set after the wake and across resets,
and the ROM, boot2, board_init() and pin set-up (AON pads) write AON registers. The SDK's `HBN_Set_HRAM_Ret()` is
only meant for the moment before HBN; the product called it at boot (aon_init) and before writing the event log
(hbn_sleep), so its flags, clock and event log would have been lost or corrupted at random on wakes.

**Fix (product and test firmware):**
- `aon_init()` (the first HBN RAM user at boot) clears RET and SLP before the first access;
- `aon_prepare_sleep()` (= `HBN_Set_HRAM_Ret()`) is the very last call in `hbn_sleep()` before
  `pm_hbn_mode_enter()`, after the last HBN RAM write; the early call before `evlog_add(EV_SLEEP)` is gone;
- aon_state, evlog and the test firmware's t021 are linker-placed in `.hbn_noinit` (bl616_leakcam.ld.in:
  NOLOAD, the top 1 KB of HBN RAM, ASSERTs), not at hand-computed addresses (user: rely on the linker);
- the SDK's fixed HBN RAM users stay below it: `hbn_ram` section +000 (low-power code, reloaded at boot),
  bl616_lp.h +000 pre-jump stub, +300 boot2 parameters, +400 low-power parameters.


## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - 3V3_SLEEP, VBAT C15.1 (BL616 only).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| BL616 step times (wake -> K230 power-on, I2C read) | not measured yet | I2C 400 kHz (now 100) | % of the top |
| Current idle / during (min / avg / peak) | 88 mA at 10:19, 86 mA at 11:09 (BL616 awake idle, K230 rails on and held in reset after pt_time; 87 mA on 2026-10-21 in the same state); the timed jobs last < 0.1 s, too short for the meter | floor (test031) | USB-C 1 VBUS |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | BL616 test firmware 149 984 B (+29 KB storage stack) | minimal image (rule) | the image this run used |
| Compile flags | BL616 SDK -Os -g, sections, --gc-sections, -flto | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

The BL616 side of every wake: wake -> decision -> K230_PWR high (adds to every session before the K230 boot starts).

## Battery energy

The BL616's current while awake (humidity slice, wake handling) and how long; energy per humidity slice (144 per day) and per wake decision.

## Obstacles

- **HBN RAM: many wrong leads before the cause** (2026-10-22, ~2 h, ~35 VMware clicks), each ruled out by a
  measurement: "HBN RAM does not keep data" (it does); "the SDK's hbn_ram copy overwrites it" (it copies 0x80
  bytes only); "boot2 passes parameters there" (address 0 on the BL616); "the heap / the start-up copy and zero
  tables" (neither covers HBN RAM); "the D-cache" (0x2000_0000-0x3000_0000 is non-cacheable); "the USB DMA"; "the
  LDO level in HBN" (1.10 V, normal). What found it: logging the value at each boot step, then a stress test per mode.
- **The wake marker in HBN_RSV3 worked, but RSV3 belongs to the ROM patch code** (aon_state.h) -> it read a stray 132
  after a reset; moved into HBN RAM.
- **My `wtest` + `reboot` run proved nothing**: the script did not wait for the console to go away, so it may have
  read the same boot's snapshot -> the scripts now wait for the port to disappear before waiting for the new one.
- **`recal` looked lost**: opening the console prints the status, which reads the probe, and the save branch
  re-stored the calibration whenever it had not restored it -> store only after a real calibration (also a
  product bug: every reading re-stored it).
- **The VMware popup for the console vanished after ~1 s** (user) and one click was missed: the port still attached;
  results only counted when the script saw the console go away and come back.
- **My first timing of the ADC spread blamed nothing in particular** -> per-run and per-step times showed it is the first
  call after each reset (the SDK calibration), not a random slow run.

- **Wake marker in HBN RAM lost** -> the SDK startup copies its hbn_ram section into HBN RAM (or boot2/ROM use it) ->
  HBN_RSV3 (AON register, kept across resets as RSV2 is).
- **HBN_RSV3 held 132 after the flash's reset** -> a reset keeps it, so a stale value read as a 1 h "wake" -> accept a
  mark only if < 2 s old.
- **`SHELL_CMD_EXPORT_ALIAS` failed on commas in the help text** (the macro splits on them) -> no commas.
- **Test firmware +29 KB** (120 784 -> 149 984 B): littlefs + easyflash + MTD (the product's storage stack), this test's Needs.

