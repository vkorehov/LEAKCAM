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

### Automated run 2026-10-25 (run.py, 378 s): pass

5 of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.

- meter K230 rails off, BL616 test firmware awake on USB: 0.061 A
- meter K230 at msh idle (agent running): 0.220 A

### Automated run 2026-10-25 (run.py, 97 s): pass

5 of 5 cold boots: SPL banner, DDR trained, OpenSBI, kernel, msh.

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.45 / 0.45 s (median / max of 5; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.44, msh 0.45); longest step to mounted 0.19 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | reset release -> OpenSBI 0.24 / 0.24 s (median / max of 5; ROM + SPL + DDR training + kernel load) (2026-10-25) | - | |
| Test run time | 378 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | n/a: boot step; the NAND read speed in the boot is test017 (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | 0.061 / 0.141 / 0.220 A at 5 V (USB-C 1 VBUS meter; K230 rails off, BL616 test firmware awake on USB: 0.061 A; K230 at msh idle (agent running): 0.220 A) (2026-10-25) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 5 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 220 mA x 4.34 V = 956 mW; per SPL stage (reset -> OpenSBI) (0.24 s) 0.23 J, 15 uAh (2026-10-25) | - | scripts/energy.py |
| Per day | 4 x SPL stage (reset -> OpenSBI) (K230 sessions, ENERGY.txt): 0.06 mAh/day, 0.09 % of 2000 mAh per month (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 33.4 / 35.8 / 35.5 C; board 28.1 / 29.5 / 29.5 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 B steady; leak: n/a, 5 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | n/a: SPL + DDR training are the floor itself (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (ROM, DDR training and NAND reads; SPL is U-Boot's own -Os build): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: ROM -> SPL banner and SPL DDR training time (now part of test022).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: superseded by test022 (boot energy).

## Obstacles

- **"boot failed with exit code 13"** = file error in the boot medium (19 = no medium) -> the ROM reads the NAND.
- **BOOT1 at 0.5 V** -> BL616 IO27 floating -> `pt_boot1` drives it low by default (not the cause; bugs/v2.txt 7).
- **Real cause** -> the EVB layout writes the byte-swapped `swap_fn_u-boot-spl.bin`; this ROM wants plain
  `fn_u-boot-spl.bin` -> genimage-spinand.cfg changed.

None in the run of 2026-10-25.

None in the run of 2026-10-25.
