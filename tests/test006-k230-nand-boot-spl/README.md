# test006: K230 NAND boot (SPL)

Status: **pass** (2026-10-25): 5 of 5 cold boots from NAND (SPL, DDR training, OpenSBI, kernel, msh); DDR 3200 MT/s all-ODT since 2026-10-23 (test025); reset
release -> OpenSBI 0.24 s.

The K230 boot ROM loads U-Boot SPL from the SPI NAND; SPL trains the DDR and loads OpenSBI and the kernel. The kernel
after OpenSBI: test007.

Boot straps: [boot_straps_board.png](boot_straps_board.png) (where), [boot_straps.png](boot_straps.png) (zoom).

## Setup

K230 boots from SPI NAND (BOOT1 low; BL616 `pt_boot1` drives it low by default). The image carries the plain
`fn_u-boot-spl.bin` at offset 0 (genimage-spinand.cfg). BL616 test firmware switches the rails; run.py does the cold
boots. Probes: Metrics.

## Needs (build features and dependencies)

K230 SPL: SPI NAND boot, DDR training (K230D_SIP_LPDDR4_3200_ALLODT, U-Boot k230d_leakcam_defconfig), TOC.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

run.py: 5 cold boots from NAND (rails off 3 s between); each boot timed to the SPL banner, DDR training, OpenSBI,
kernel and msh; meter readings with the K230 rails off and at msh idle.

## Pass

SPL boots from NAND, DDR trains, OpenSBI and the kernel follow, msh comes up: on every cold boot.

## Result

- SPL boots from NAND, DDR trains (3200 MT/s all-ODT; the PMU's "Firmware run has completed" line), `Winbond SPI NAND was found`, OpenSBI v0.9 follows (first run
  2026-10-17).
- 5 of 5 cold boots reach msh; reset release -> OpenSBI 0.24 s, pt_run -> msh 0.45 s (2026-10-25, run.py).
- The ROM needs the plain `fn_u-boot-spl.bin`, not the EVB's byte-swapped `swap_fn_u-boot-spl.bin`
  (genimage-spinand.cfg; 2026-10-17).

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

Metrics (2026-10-25, run.py, median / max of 5): reset release -> OpenSBI 0.24 / 0.24 s (ROM + SPL + DDR training +
kernel load); pt_run -> msh 0.45 / 0.45 s. The whole boot is test022.

## Battery energy

Metrics (2026-10-25): SPL stage 0.23 J, 15 uAh per boot; 0.06 mAh/day at 4 sessions. The whole boot: test022.
