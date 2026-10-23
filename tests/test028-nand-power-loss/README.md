# test028: NAND / UFFS power-loss robustness

Status: **short run pass** (2026-10-21): 13 random cuts, 0 lost or corrupt; the 100-cut run still to do.

The product writes clips, logs and references to /sdcard (UFFS on NAND) while the battery can run out or the BL616
can cut the rails. UFFS is log-structured; this proves it on rev 1.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

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

## Results (2026-10-21, image with /sdcard 32 MiB, NAND 80 MHz)

Writer and checker: `plt_write` / `plt_check` in [leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c) (SDK
copy): files of 1 KB..2 MB in /sdcard/plt (16 slots), each fsynced, then a commit record (two slots, fsynced).
`plt_check` verifies the 15 newest committed files (the 16th slot may be the one cut mid-write). Host loop:
[plt_loop.py](plt_loop.py) (BL616 `pt_off` / `pt_on` / `pt_run`, K230 console `/dev/ttyACM1`), log
[run1_2026-10-21.csv](run1_2026-10-21.csv).

| | Result |
|---|---|
| Power cuts | 13 (2 trial + 11; stopped after 5 min, user: longer run later) at 2.9-18.8 s into the writer |
| Mount after each cut | 13 / 13, every `plt_check` ran |
| Committed files verified | 15 per cycle, **0 bad**; every "committed" line printed before a cut was found afterwards (last = previous last + commits) |
| Free space | 16.5-18.3 MB of 32 MB, no downward trend (varies with which slots hold the 1-2 MB files) |
| Writer speed | 2 MB file 1.47 s with fsync (~1.4 MB/s through UFFS) |

Not yet: the 100-cut run; the mount time after an unclean cut (the loop's `boot_times` parse came back empty: to
fix before the long run; a clean boot of this image: mounts 0.374 s, app init 0.660 s, test022).

## Metrics (2026-10-21, short run)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.660 s (clean boot, this image) | test022 | mounts 0.374 s (/sdcard 32 MiB) |
| This part's boot share | after an unclean cut: not parsed yet | - | |
| Test run time | 5 min, 13 cuts (~25 s per cycle) | - | setup: build + flash ~6 min |
| Recovery after a cut | every mount clean, no fsck-like delay seen | - | |
| Current | msh idle with charging 540 mA (USB VBUS, D1 on); during writes not read | floor 202 mA w/o charging | |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a | - | |
| Per day | n/a | - | |
| Temperature die / board | not measured: missed in the short run (taken in the long run) | - | |
| Image (sizes) | kdimg 58.4 MB (/sdcard 32 MiB, /bin 20 MiB), kernel 3.75 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Mount time after an unclean power cut (UFFS recovery may scan more) vs a clean mount.

## Battery energy

n/a: a robustness test (the energy of writes is test017).

## Obstacles

None yet.
