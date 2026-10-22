# test003: K230 rails and USB boot, 2026-10-17

Status: **pass; USB-C attach with rails on: bug-002** (2026-10-17).

The cell on JB1, `bl616_powertest` on PR1. Probe points and limits: [k230_rails.png](k230_rails.png).

## Setup

Not written at the time; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

BL616 test firmware PT_TEST_POWER (GPIO K230_PWR/RSTN); K230 boot ROM USB mode (no K230 build).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

Not written at the time; the steps are the rows of the Results tables.

## Pass

Not written at the time; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| `pt_status` (rails off) | K230 rails off, reset held | pass |
| `pt_on` | `3V3 up 1.726 ms after K230_PWR` (estimate ~3 ms; soft start) | pass |
| K230_PWR, 0V8, 3V3, 1V8, 1V1, RSTN, 0V8_MIPI, 1V8_MIPI, 1V8_DDR | all within the limits (multimeter) | pass |
| PG_1V8 | not probed (pad too small); implied: 1V1 is up and U7 starts only on PG_1V8 | pass |
| `pt_off` | `3V3 gone 12 ms` (expected 56-82 ms through RD1/RD2) | note |
| USB-C 1 to a Mac with the rails on | VUSB ~2.2 V, the Mac never turns on VBUS: [bug-002](../../bugs/bug-002/README.md) | **fail** |
| `pt_off`, cable in, `pt_on`, `pt_run` | VUSB 5 V; the K230 ROM enumerates as `29f1:0230 Canaan K230 USB Boot Device` | pass |
| Debug serial on USBC2 | CH340 on `/dev/ttyUSB0`; blank NAND: ROM prints `boot failed with exit code 13` at 115200 | pass |

**Pass** (all four K230 rails, boot ROM, debug UART); USB-C attach with the rails on: bug-002 open.

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (to fill on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - K230 rails 0V8 / 1V1 / 1V8 / 3V3 (k230_rails.png), VBAT C15.1.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | |
| Test run time | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | not measured: run before the metrics rule (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | not measured: run before the metrics rule (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | not measured: run before the metrics rule (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: rails on -> reset released (BL616 pt_on/pt_run timing; RSTN 12.5 ms after the last rail by design).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: current with the K230 rails on and the K230 held in reset (the cost of `pt_on` alone).

## Obstacles

- **Mac on USB-C 1 gives no power with rails on** -> CH340X D+ back-feeds VUSB via D3 (~2.2 V); the USB-C source
  waits for < 0.8 V -> plug USB before `pt_on`; rev 2 fix (bugs/v2.txt).
- **PG_1V8 pad too small** -> implied by 1V1 being up.
- **`pt_off` 12 ms vs 56-82 ms** -> noted, harmless.
