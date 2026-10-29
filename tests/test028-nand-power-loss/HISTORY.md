# test028: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Status line before the 2026-10-29 cleanup

Status: **pass** (2026-10-23): 500 random power cuts during writes (100 + 400), 0 lost or corrupt; 4 check results unparsed (cause open, no loss); app init after a cut 0.478 s (short run 2026-10-21: 13 cuts).

### Automated run 2026-10-25 (run.py, 435 s): pass

20 random cuts during plt_write ([run4_2026-10-25.csv](run4_2026-10-25.csv)): 0 bad files, 0 checks unparsed, 638 commits before the cuts; app init after a cut 0.330-0.520 s; free 25216 -> 17280 KB; plt_check after the run: clean.

### 400-cut run, 2026-10-23 17:50-20:15 (run3_2026-10-23.csv, unattended)

400 cuts, **0 bad**, 16 040 commits before cuts, every one found (last record 19 917); free 16.5-18.6 MB, no trend;
app init after a cut 0.468-0.487 s (median 0.477, 399 boots: DDR retrained each time, no boot failure). **4 cycles
(66, 106, 142, 174) "mount/check failed"**: `plt_check`'s result line was not parsed in 30 s (those cycles took ~40-50 s
against ~25 s); no data lost: the next check counts exactly the commits of both cycles (e.g. 6317 + 63 + 1 = 6381).
Cause open (a slow check? text lost on the console?): the loop now keeps the raw text of a failed check
(`<csv>.fail.txt`).

Pass. The app init after a cut (0.478 s) is this image (it mounts the 32 MiB `/sdcard`): it was 0.660 s on a clean
boot with the 115200 console (test022); the 0.30 s of test039 is the minimal image without that mount.

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

### Results (2026-10-21, image with /sdcard 32 MiB, NAND 80 MHz)

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

### Superseded metric values

Metrics heading was "## Metrics (2026-10-21, short run)".

- 2026-10-21: Boot, reset release -> app init 0.660 s (clean boot, this image); mounts 0.374 s (/sdcard 32 MiB).
- 2026-10-21: This part's boot share: after an unclean cut: not parsed yet.
- 2026-10-21: Test run time 5 min, 13 cuts (~25 s per cycle); setup: build + flash ~6 min.
- 2026-10-21: Current: msh idle with charging 540 mA (USB VBUS, D1 on); during writes not read; floor 202 mA w/o
  charging.
- 2026-10-23: Current, 100-cut run: 507-537 mA VBUS (peak 537), cell charging the whole run while the K230 cycles;
  USB-C 1 on the Mac kept VBUS on with the K230 up: no bug-002 effect without the CH340.

### Former placeholder

- Setup: **MISSING**: hookup, firmware / image, and the probes named in Metrics.

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
