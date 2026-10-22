# test021: BL616: wake latency, flash storage, sensor reads

Status: **pass** (2026-10-22): RTC wake -> main() 12-16 ms (one 245 ms outlier, open), 3V3 ramp 1.7 ms, AHT20 50 ms, easyflash key set 1 ms / get 0.6 ms; probe ADC 3-32 ms spread open.

The always-on side's speed: how fast a wake turns into a K230 power-on, and the cost of its own jobs.

## Setup

BL616 test firmware (tools/power-bringup/bl616_powertest) with `pt_time` (PT_TEST_TIMING): min / max of 5 runs on the
core timer (us). Wake latency: `pt_sleep 5` writes the RTC tick the wake is due at into HBN_RSV3 (an AON register
the SDK leaves alone; HBN RAM did not keep it), `main()` reads the RTC (30.5 us ticks) and the core timer before
`board_init()`. K230 off except for the 3V3 ramp. USB-C 1 powers the board.

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
| AHT20 trigger -> data | 50.2-51.2 ms | the driver's first read at 50 ms (datasheet 80 ms max); worth a status poll from ~40 ms |
| Leak probe ADC (init + 6 conversions, the first 5 dropped) | **3.1 .. 31.8 ms** | one of 5 runs ~10x slower every time: open (in the leak wake path) |
| easyflash on littlefs: mount | 4.3-4.8 ms; 63 ms the first time (empty PSM, reformatted) | |
| easyflash 64 B key: set / get / delete | 0.9-1.6 ms (once 20-24 ms: littlefs compaction) / 0.6-1.3 ms / 1.0-1.4 ms | 25 of 25 read back right |

Wake to K230 power-on on the BL616 side: ~15 ms + the product's decision + 1.7 ms ramp; the K230 boot (0.48 s, test022)
dominates.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - 3V3_SLEEP, VBAT C15.1 (BL616 only).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| BL616 step times (wake -> K230 power-on, I2C read) | not measured yet | I2C 400 kHz (now 100) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
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

- **Wake marker in HBN RAM lost** -> the SDK startup copies its hbn_ram section into HBN RAM (or boot2/ROM use it) ->
  HBN_RSV3 (AON register, kept across resets as RSV2 is).
- **HBN_RSV3 held 132 after the flash's reset** -> a reset keeps it, so a stale value read as a 1 h "wake" -> accept a
  mark only if < 2 s old.
- **`SHELL_CMD_EXPORT_ALIAS` failed on commas in the help text** (the macro splits on them) -> no commas.
- **Test firmware +29 KB** (120 784 -> 149 984 B): littlefs + easyflash + MTD (the product's storage stack), this test's Needs.

