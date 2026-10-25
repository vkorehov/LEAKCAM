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

### Automated run 2026-10-25 (run.py, 295 s): pass

cp /sdcard/app/leakcam_wake -> RAM: 0.49, 0.49, 0.36 s; RAM -> /sdcard: 3.31, 3.08, 2.50 s; read 14.4 MB/s, write 2.1 MB/s.

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)
- meter K230 copying RAM -> /sdcard over and over (cp): 0.259 A

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.45 / 0.45 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.44, msh 0.45); longest step to mounted 0.19 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | UFFS mounts in the boot: /bin + /sdcard done at 0.31 s (test022 boot_times) (2026-10-25) | - | |
| Test run time | 295 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | read 14.4 MB/s, write 2.1 MB/s (shell cp, best of 3, host-timed incl. ~0.1 s prompt); user read() 12.7 MB/s = 81 % of test017's raw 15.6 MB/s (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | 0.220 / 0.239 / 0.259 A at 5 V (USB-C 1 VBUS meter; K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl); K230 copying RAM -> /sdcard over and over (cp): 0.259 A) (2026-10-25) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 259 mA x 4.35 V = 1128 mW; per 5 MB clip write (2.50 s) 2.82 J, 180 uAh; over idle (220 mA) 0.42 J (2026-10-25) | - | scripts/energy.py |
| Per day | 4 x 5 MB clip write: 0.72 mAh/day, 1.08 % of 2000 mAh per month (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 39.8 / 48.2 / 48.2 C; board 32.4 / 38.1 / 38.1 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 7025152 / 7312064 B; BL616 no leak (mem_leak.py, 9 snapshots); K230 no leak (mem_leak.py, 9 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | n/a: UFFS on /sdcard is part of the product floor (clips are stored) (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | kernel stays -O1: -O2 faults in the LWP signal return (test008 Flags A, 2026-10-22), kept -O1 by the user's decision; this result is kernel/driver code, no faster build to compare until that is fixed (2026-10-25) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: the UFFS mount time of /sdcard and /bin, empty and full (a full /sdcard may mount slower: tree build scans blocks).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: extra current while reading and writing /sdcard vs idle; energy per MB written (clips) and read.

## Obstacles

- **`echo` cannot create files** -> msh `echo` opens without O_CREAT -> create with `cp` first.
- **No checksum tool on the K230** -> byte-exact check with `hexdump` of a 100 KB file, compared on the host.
- **Dump looked corrupted after boot** -> the agent's `READY not answered` lines interleave -> dump on a quiet console.

None in the run of 2026-10-25.
