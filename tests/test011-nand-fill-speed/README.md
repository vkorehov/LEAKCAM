# test011: NAND persistence, full disk, speed, 2026-10-18

Status: **pass** (2026-10-18).

K230 shell (`cp`, `df`, `rm`, `hexdump`). `/sdcard` = nand1, UFFS, 169 MB free when empty.

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

UFFS on SPI NAND, dfs, msh file commands.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Test | Result | |
|---|---|---|
| Write a file, power-cycle the K230 (`pt_off`, `pt_on`, `pt_run`), read it | 953 bytes, marker intact | pass |
| Fill with 5,180,912-byte copies | 33 copies (171 MB) to 3.8 MB free, no errors; 3.4-4.5 s per copy | pass |
| Copy past full | `Write file data failed`, partial file, last 128 KB block kept in reserve; other files intact | pass |
| Read (`/sdcard` -> RAM `/tmp`), 5,180,912 B, shell `cp` | 0.55 s = **9.4 MB/s** (3 runs) | |
| Write (RAM `/tmp` -> `/sdcard`), 5,180,912 B | 2.82 s = **1.8 MB/s** (2 runs) | |
| Byte-exact: 100,376-byte copy on the nearly full disk, hexdump vs original | 6274 / 6274 lines identical, again after a power cycle | pass |
| Read from a user program (64 KB `read()`s), 5,180,912 B | 195 s (26 KB/s) before the poll patch (test012); after: 0.41 s = **12.7 MB/s** | pass |

**Pass.**

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

Not recorded (before the boot rule, 2026-10-21). If re-run: the UFFS mount time of /sdcard and /bin, empty and full (a full /sdcard may mount slower: tree build scans blocks).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: extra current while reading and writing /sdcard vs idle; energy per MB written (clips) and read.

## Obstacles

- **`echo` cannot create files** -> msh `echo` opens without O_CREAT -> create with `cp` first.
- **No checksum tool on the K230** -> byte-exact check with `hexdump` of a 100 KB file, compared on the host.
- **Dump looked corrupted after boot** -> the agent's `READY not answered` lines interleave -> dump on a quiet console.
