# test004: K230 hello from SRAM

Status: **pass** (2026-10-25): k230_hello loads over USB into SRAM in 4.2 s and runs (3 of 3 boots), K230_ALIVE toggles.

[k230_hello](../../tools/power-bringup/k230_hello): 0.8 KB bare-metal program the boot ROM loads into SRAM over
USB-C 1 (`make run`), nothing written to the NAND.

## Setup

K230 in USB boot on USB-C 1; k230_hello built in tools/power-bringup/k230_hello; BL616 `pt_status` to read K230_ALIVE.

## Needs (build features and dependencies)

K230 ROM USB boot, SRAM image (no RT-Smart).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. `make run` with the K230 in USB boot: the ROM loads k230_hello into SRAM and starts it.
2. Read K230_ALIVE in `pt_status`.

## Pass

Step 1 prints `LEAKCAM K230 hello: running from SRAM, hart 0`, then `status n=... mcycle=...` once a second;
step 2: K230_ALIVE toggles.

## Result

- Pass on 3 of 3 USB boots: k230_hello loaded in 4.2 s each, K230_ALIVE half-period 1000 ms (run.py, 2026-10-25).
- Status lines 800 M cycles apart: the core runs at 800 MHz from the ROM (first run, 2026-10-17).

Open:
- none.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | n/a: USB boot of a bare-metal program; load + start 4.2 s (make run, host) (2026-10-25) | test022 | longest step: |
| This part's boot share | n/a: no RT-Smart boot (2026-10-25) | - | |
| Test run time | 170 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | K230_ALIVE half-period avg 1000, 1000, 1000 ms (program: 1000 ms per line) (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | not read in the run (meter prompts off); nearest states: K230 in reset 0.086 A (test022), msh idle 0.220 A (test006) (2026-10-25, from recorded readings) | floor (test031) | at the probe above |
| Voltage under load (min) | n/a: the bare-metal hello has no ADC driver; VBAT at msh idle 4.34 V (test007) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: bring-up program (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: bring-up program, never in a session (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 30.4 / 32.4 / 32.4 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; leak: n/a, 3 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | bare-metal k230_hello.bin 789 B, loaded over USB into SRAM; never on NAND (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: bring-up program only (-O2 already) (2026-10-25) | the faster one stays | |

## Boot time

n/a: a bring-up step outside the product boot.

## Battery energy

n/a: a one-off bring-up step, no product activity.
