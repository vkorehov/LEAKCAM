# test028: NAND / UFFS power-loss robustness

Status: **pass** (2026-10-25): 520 random power cuts during writes over all runs (100 + 400 on 2026-10-23, 20 on
2026-10-25), 0 fsynced files lost or corrupt; app init after a cut 0.330-0.520 s.

The product writes clips, logs and references to /sdcard (UFFS on NAND) while the battery can run out or the BL616
can cut the rails. UFFS is log-structured; this proves it on rev 1.

## Setup

Board on USB-C 1, K230 console on `/dev/ttyACM1` (BL616 link), BL616 test firmware (`pt_off` / `pt_on` / `pt_run`).
K230 image with /sdcard 32 MiB, NAND 50 MHz. Writer and checker: `plt_write` / `plt_check` in
[leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c) (SDK copy): files of 1 KB..2 MB in /sdcard/plt (16
slots), each fsynced, then a commit record (two slots, fsynced). `plt_check` verifies the 15 newest committed files
(the 16th slot may be the one cut mid-write). Host loop: [plt_loop.py](plt_loop.py) (long runs, keeps the raw text of
a failed check in `<csv>.fail.txt`) or run.py (20 cuts); logs run*_<date>.csv.

## Needs (build features and dependencies)

UFFS on nand1, dfs_posix (open/write/fsync/statfs), plt_write/plt_check (leakcam_bench.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. A test hook writes files in a loop (sizes 1 KB .. 2 MB, fsync, verify the previous file).
2. BL616 test firmware cuts the rails at random moments (`pt_off` after a random 2-20 s, `pt_on`, `pt_run`), 100
   times, console recorded.
3. After each boot: mount OK, the files written before the last fsync intact, free space not shrinking.

## Pass

100 random power cuts: every mount succeeds, no fsynced file lost or corrupt, no space leak.

## Result

- 520 cuts in total, every mount clean, **0 bad** committed files, every commit printed before a cut found afterwards
  (100 cuts 2026-10-23, 400 cuts 2026-10-23, 20 cuts run.py 2026-10-25); no fsck-like delay.
- Free space 16.5-18.7 MB of 32 MB over the long runs, no downward trend (2026-10-23).
- App init after an unclean cut 0.330-0.520 s (run.py, 2026-10-25); DDR retrained on every boot, no boot failure
  in 399 boots (2026-10-23).
- Writer speed: 2 MB file 1.47 s with fsync (~1.4 MB/s through UFFS) (2026-10-21).

- The run removes its 16 `/sdcard/plt` files at the end (2026-10-30): left behind (~7 MB) with the small /sdcard
  they let test105's encoder streams fill it and stall the encoder (test027).
- 2026-10-30 at NAND 50 MHz: 20 cuts, 0 bad files, app init after a cut 0.610-0.630 s, plt_check clean.

Open:
- 4 of 400 checks unparsed in the 400-cut run (2026-10-23, cycles took ~40-50 s against ~25 s), no data lost; cause
  open (a slow check? console text lost?); 0 unparsed in the 20-cut run of 2026-10-25.
- Temperature not in this README (missed in the short run of 2026-10-21).

## Metrics (date per value)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.330-0.520 s after an unclean cut (20 cuts, run.py, 2026-10-25) | test022 | /sdcard 32 MiB mounted |
| This part's boot share | after an unclean cut: app init 0.330-0.520 s (2026-10-25); no clean-boot figure on that image | - | |
| Test run time | 435 s automated, 20 cuts (run.py, 2026-10-25) | - | setup: build + flash ~6 min |
| Recovery after a cut | every mount clean, no fsck-like delay seen | - | |
| Current | 507-537 mA VBUS (peak 537) during the 100-cut run, cell charging the whole run while the K230 cycles (2026-10-23) | floor 202 mA w/o charging | USB-C 1 VBUS |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a | - | |
| Per day | n/a | - | |
| Temperature die / board | not measured: missed in the short run (taken in the long run) | - | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 207136 / min 204396 / 207136 B, blocks 19 -> 19, deleted 0; K230 heap available 7025152 / 7023168 / 7312064 B; BL616 no leak (mem_leak.py, 8 snapshots); K230 no leak (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 58.4 MB (/sdcard 32 MiB, /bin 20 MiB), kernel 3.75 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | kernel test commands in leakcam_bench.o: 11.2 KB code + 0.6 KB RAM for all 13 (ddr/nand/uart_bench, boot_times, plt_*, rtc_*, pad_read, bat_watch); no init, 0 ms boot; not in the product image (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (NAND program/erase and UFFS commit, kernel -O1 (see test007)): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

Mount time after an unclean power cut (UFFS recovery may scan more) vs a clean mount.

## Battery energy

n/a: a robustness test (the energy of writes is test017).
