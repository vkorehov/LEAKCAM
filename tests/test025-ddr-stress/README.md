# test025: DDR stress (128 MB, 3200 MT/s all-ODT)

Status: **pass at 3200 MT/s all-ODT** (2026-10-25: 10 passes, 0 errors; 8 h soak 2026-10-23/24: 5230 passes, 0
errors, 0 resets). Open: the run with the KPU and the encoder busy.

SPL trains the DDR on every boot (3200 MT/s all-ODT, u-boot k230d_leakcam_defconfig). A marginal setting shows only
as rare random crashes. This test runs a pattern memory test over most of the free RAM for a long time.

## Setup

The test image with [memtest/](memtest/memtest.c) on /sdcard (SDK copy only), DDR setting from the u-boot
k230d_leakcam_defconfig; NAND boot from the BL616 test firmware; the console recorded (`console_record.py`); meter in
USB-C 1 VBUS (Probes in Metrics).

## Needs (build features and dependencies)

MMZ memory, memtest app (temporary), cache control.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. [memtest/](memtest/memtest.c), a temporary user program (SDK copy only: `src/applications/memtest` +
   `subdirs-y += memtest` in apps.mk): all of RT-Smart's system RAM it can get through malloc (cached, 4 MB left
   free) and all of the MMZ media pool through `kd_mpi_sys_mmz_alloc` (uncached), 1 MB blocks; per pass walking
   one, walking zero, address, inverse address and random patterns; every mismatch printed with region, address,
   pattern. `/sdcard/app/memtest [passes] [reserve_mb]`, 0 passes = until reset.
   The K230D's DDR is 128 MB (CONFIG_MEM_TOTAL_SIZE 0x8000000): RT-Smart 58 MB (kernel heap 8 MB of it), MMZ
   70 MB from 0x3A00000. A kernel command could only reach the 8 MB heap.
2. Run it in a loop for >= 1 h, the console recorded (skill k230-rtsmart-hangs, `console_record.py`).
3. Repeat with the KPU and the encoder busy (heat, power noise) if the first run is clean.
4. Repeat after a few cold starts (each start retrains).

## Pass

0 errors over >= 1 h and >= 5 cold starts; no reset (a silent reset = watchdog: the test must sleep between passes).

## Result

- 3200 MT/s all-ODT (`K230D_SIP_LPDDR4_3200_ALLODT`) is the board's DDR setting: training passes (app init 0.345 s);
  write 6.08, read 4.84, copy 2.77 GB/s (+50 / +18 / +49 % over 2667); 8 h soak: 5230 passes x 109 MB x 5 patterns
  (~570 GB), 0 errors, 0 resets, 198 MB/s write+verify (2026-10-23/24).
- 3200 HW-ODT (`K230D_SIP_LPDDR4_3200_HWODT`) fails: one bit flipped in kernel text in DDR, Illegal Instruction at
  the kernel's start (2026-10-23).
- Soak temperatures, open board, room ~29 C: K230 die 60.5 C peak (after ~2 h), 56.6-58.4 C later; board (AHT20)
  43.8 C peak (2026-10-23/24).
- memtest covers 109 of 128 MB (40 MB system RAM, 4 MB left free, + 69 MB MMZ) (2026-10-21).
- 10 passes, 0 errors in 61 s (run.py, 2026-10-25).
- 5 cold starts (DDR retrained each), 0 errors, at 2667 MT/s (2026-10-21).

Open:
- Step 3: memtest with the KPU and the encoder busy. test105's 10 min full load (2026-10-30) ran memtest that way
  (memtest started last, 0 K230 resets), but its console log was overwritten by a later ad-hoc run: no error count.
- The >= 5 cold-start set (step 4) at 3200 MT/s: dozens of cold boots trained DDR fine on 2026-10-30 (test017,
  test022, test105), but without memtest after each.
- Current and energy at 3200 MT/s (memtest alone).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init (median of 5) | 0.345 s at 3200 all-ODT (2026-10-23) | test022 | longest step: |
| This part's boot share | **MISSING**: SPL's DDR training at 3200 MT/s not split out (the 2667 figure superseded) | - | |
| Test run time | 77 s automated (run.py, 2026-10-25); 8 h soak 22:40:56 - 06:41:04 (2026-10-23/24) | - | setup separately |
| memtest MB/s, errors | 198 MB/s write+verify (5 patterns, MMZ uncached), 0 errors (3200 all-ODT, 8 h, 2026-10-23/24) | DDR top 5.33 GB/s (test016) | a checker, not a bandwidth test |
| Current idle / during | idle 220 mA (test006); memtest alone at 3200 MT/s **MISSING** (2667 figures superseded; test105 has memtest only on top of other loads) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT ~4.15 V (full cell) |
| Voltage under load (min) | VBAT 4.33 V min (K230 ADC ch 1, test023 calibration, 3 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING**: re-measure at 3200 MT/s | - | by hand: scripts/energy.py does not exist |
| Per day | n/a: a bench test, not in the product | - | |
| Temperature die / board (start / peak / end) | die 34.6 / 39.8 / 39.8 C; board 26.9 / 29.4 / 29.4 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 7025152 / 7312064 B; leak: n/a, 3 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | memtest 67 120 B on /sdcard (test only); 0 ms boot (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (DDR bandwidth bounds memtest): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Each DDR setting's training time (with test016), cold starts recorded.

## Battery energy

Battery current at 2667 and 3200 MT/s under the same load: the cost of the faster DDR per second, against the time it saves (test016, test022).
