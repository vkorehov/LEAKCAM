# test003: K230 rails and USB boot

Status: **pass** (2026-10-25): all K230 rails within limits, 3V3 up 1.69-1.70 ms after K230_PWR, the K230 USB boot
ROM enumerates; USB-C attach with the rails on (bug-002) solved by the U2 1-2-3 bridge.

The cell on JB1, `bl616_powertest` on PR1. Probe points and limits: [k230_rails.png](k230_rails.png).

## Setup

**MISSING**; the setup is in the introduction above.

## Needs (build features and dependencies)

BL616 test firmware PT_TEST_POWER (GPIO K230_PWR/RSTN); K230 boot ROM USB mode (no K230 build).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the checks listed in Result.

## Pass

**MISSING**; every Result line carries its pass / fail.

## Result

- K230 rails off, reset held at `pt_status`; K230_PWR, 0V8, 3V3, 1V8, 1V1, RSTN, 0V8_MIPI, 1V8_MIPI, 1V8_DDR all
  within the limits (multimeter): pass (2026-10-17).
- 3V3 up 1.69-1.70 ms after K230_PWR, off after `pt_off` every time, 8 rail cycles: pass (2026-10-25, run.py).
- PG_1V8 not probed (pad too small); implied: 1V1 is up and U7 starts only on PG_1V8 (2026-10-17).
- `pt_off`: `3V3 gone 12 ms` (expected 56-82 ms through RD1/RD2): noted, harmless (2026-10-17).
- USB-C 1 to a Mac with the rails on: VUSB ~2.2 V, the Mac never turns on VBUS ([bug-002](../../bugs/bug-002/README.md)),
  solved by the U2 1-2-3 bridge (2026-10-25); with `pt_off`, cable in, `pt_on`, `pt_run`: VUSB 5 V (2026-10-17).
- The K230 ROM enumerates as `29f1:0230 Canaan K230 USB Boot Device`: pass (2026-10-25, run.py).
- Debug serial on USBC2: CH340 on `/dev/ttyUSB0`; blank NAND: ROM prints `boot failed with exit code 13` at 115200:
  pass (2026-10-17).

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - K230 rails 0V8 / 1V1 / 1V8 / 3V3 (k230_rails.png), VBAT C15.1.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | n/a: rails test; reset release -> ROM on USB seen within 10 s (2026-10-25) | test022 | longest step: |
| This part's boot share | K230_PWR -> 3V3 up 1.69-1.70 ms (pt_on, 8 cycles) (2026-10-25) | - | |
| Test run time | 220 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | n/a: rails test (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | rails off 0.061 A (BL616 awake on USB, test006 2026-10-25); rails on, K230 in reset 0.086 A (test022, 2026-10-21) (2026-10-25, from recorded readings) | floor (test031) | at the probe above |
| Voltage under load (min) | n/a: no K230 ADC reading with the K230 in reset or in its ROM; VBAT at msh idle 4.34 V (test007) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the rails' share is inside the boot energy (test007: 28 uAh per cold boot) | - | scripts/energy.py |
| Per day | n/a: the rails come up once per session; the session energy is test102 (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 34.9 / 34.9 / 31.9 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; BL616 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | BL616 test firmware commands (pt_*); test firmware 1 114 864 B; not in the product firmware (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (regulator ramps): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: rails on -> reset released (BL616 pt_on/pt_run timing; RSTN 12.5 ms after the last rail by design).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: current with the K230 rails on and the K230 held in reset (the cost of `pt_on` alone).
