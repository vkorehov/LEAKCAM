# test006: K230 NAND boot (SPL), 2026-10-17

Status: **pass** (2026-10-17).

Boot straps: [boot_straps_board.png](boot_straps_board.png) (where), [boot_straps.png](boot_straps.png) (zoom).

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

K230 SPL: SPI NAND boot, DDR training (K230D_SIP_LPDDR4_2667), TOC.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| Full image, reset (BOOT1 low) | ROM: `boot failed with exit code 13` (file error in the boot medium) | **fail** |
| BOOT1 read 0.5 V with IO27 parked; driven low (`pt_boot1`), ~0 V | still 13 | not the cause |
| Plain SPL (`fn_u-boot-spl.bin`) at offset 0 | `U-Boot SPL 2022.10`, DDR training `Firmware run has completed 2667`, `Winbond SPI NAND was found`, `OpenSBI v0.9` | pass |

**Pass:** SPL boots from NAND, DDR trains at 2667. The kernel after OpenSBI: test007.

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | **MISSING** (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | **MISSING** (2026-10-21); fill on a re-run | - | |
| Test run time | **MISSING** (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | **MISSING** (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | **MISSING** (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | **MISSING** (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | **MISSING** (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: ROM -> SPL banner and SPL DDR training time (now part of test022).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: superseded by test022 (boot energy).

## Obstacles

- **"boot failed with exit code 13"** = file error in the boot medium (19 = no medium) -> the ROM reads the NAND.
- **BOOT1 at 0.5 V** -> BL616 IO27 floating -> `pt_boot1` drives it low by default (not the cause; bugs/v2.txt 7).
- **Real cause** -> the EVB layout writes the byte-swapped `swap_fn_u-boot-spl.bin`; this ROM wants plain
  `fn_u-boot-spl.bin` -> genimage-spinand.cfg changed.
