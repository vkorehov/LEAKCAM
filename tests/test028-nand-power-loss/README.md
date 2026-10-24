# test028: NAND / UFFS power-loss robustness

Status: **pass** (2026-10-23): 500 random power cuts during writes (100 + 400), 0 lost or corrupt; 4 check results unparsed (cause open, no loss); app init after a cut 0.478 s (short run 2026-10-21: 13 cuts).

The product writes clips, logs and references to /sdcard (UFFS on NAND) while the battery can run out or the BL616
can cut the rails. UFFS is log-structured; this proves it on rev 1.

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

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

### 100-cut run, 2026-10-23 16:37-17:22 (run2_2026-10-23.csv)

Board on USB-C 1 (Mac), K230 console on `/dev/ttyACM1` (no clicks), CH340 off the board.

| What | Result |
|---|---|
| Cuts | **100 / 100**, each at a random point 2.1-19.6 s into `plt_write` |
| Mount after each cut | 100 / 100, every `plt_check` ran |
| Committed files verified | 15 per cycle, **0 bad**; 3910 commits printed before cuts, last record 3924 = all found |
| Free space | 16.5-18.7 MB of 32 MB, no downward trend |
| Boot after an unclean cut | env init 0.459-0.477 s (median 0.469), app init **0.468-0.486 s** (median 0.478) |
| Cycle | ~20 s; one 9 min gap after cycle 50 = the Linux VM paused (no log 16:55-17:04, "Clock change detected"), not the board |

### 400-cut run, 2026-10-23 17:50-20:15 (run3_2026-10-23.csv, unattended)

400 cuts, **0 bad**, 16 040 commits before cuts, every one found (last record 19 917); free 16.5-18.6 MB, no trend;
app init after a cut 0.468-0.487 s (median 0.477, 399 boots: DDR retrained each time, no boot failure). **4 cycles
(66, 106, 142, 174) "mount/check failed"**: `plt_check`'s result line was not parsed in 30 s (those cycles took ~40-50 s
against ~25 s); no data lost: the next check counts exactly the commits of both cycles (e.g. 6317 + 63 + 1 = 6381).
Cause open (a slow check? text lost on the console?): the loop now keeps the raw text of a failed check
(`<csv>.fail.txt`).

Pass. The app init after a cut (0.478 s) is this image (it mounts the 32 MiB `/sdcard`): it was 0.660 s on a clean
boot with the 115200 console (test022); the 0.30 s of test039 is the minimal image without that mount.

## Metrics (2026-10-21, short run)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.660 s (clean boot, this image) | test022 | mounts 0.374 s (/sdcard 32 MiB) |
| This part's boot share | after an unclean cut: not parsed yet | - | |
| Test run time | 5 min, 13 cuts (~25 s per cycle) | - | setup: build + flash ~6 min |
| Recovery after a cut | every mount clean, no fsck-like delay seen | - | |
| Current | msh idle with charging 540 mA (USB VBUS, D1 on); during writes not read | floor 202 mA w/o charging | |
| Current, 100-cut run (2026-10-23) | 507-537 mA VBUS (peak 537), cell charging the whole run while the K230 cycles | - | USB-C 1 on the Mac kept VBUS on with the K230 up: no bug-002 effect without the CH340 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a | - | |
| Per day | n/a | - | |
| Temperature die / board | not measured: missed in the short run (taken in the long run) | - | |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 58.4 MB (/sdcard 32 MiB, /bin 20 MiB), kernel 3.75 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Mount time after an unclean power cut (UFFS recovery may scan more) vs a clean mount.

## Battery energy

n/a: a robustness test (the energy of writes is test017).

## Obstacles

- **2026-10-23 09:46, the 100-cut run was started while the BL616 was still in a test038 lockout session** ->
  stopped after 0 cycles (csv header only). At 09:47:08 that session's 60 s heartbeat limit ran out: rails off, then
  the lockout HBN, so the BL616 left USB (not a crash, not this test). Details and the fix: tests/test038, Obstacles.
  Rerun the loop on its own, on the charger or a full cell.
- **4 of 400 checks unparsed in the 400-cut run** (2026-10-23 cycles 66, 106, 142, 174, ~40-50 s against ~25 s) -> the
  loop's 30 s read of `plt_check` ended before its result line; no data lost (the next check counted both cycles'
  commits) -> cause open; the loop now keeps the raw text of a failed check (`<csv>.fail.txt`) for the next run.
- **A 9 min gap after cycle 50 of the 100-cut run** (2026-10-23 16:55-17:04) -> the Linux VM was paused (no log, then
  "Clock change detected"), not the board -> keep the Mac awake for long runs; the loop simply continues.
