# test011: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 64 s): pass

cp /sdcard/app/leakcam_wake -> RAM: 0.57, 0.57, 0.57 s; RAM -> /sdcard: 3.84, 3.14, 2.77 s; read 9.0 MB/s, write 1.9 MB/s.


### Automated run 2026-11-11 (run.py, 61 s): **FAIL**

cp /sdcard/app/leakcam_wake -> RAM: 0.58, 0.58, 0.12 s; RAM -> /sdcard: 3.84, 3.14, 0.43 s; read 44.1 MB/s, write 12.2 MB/s.


### Automated run 2026-11-11 (run.py, 78 s): **FAIL**

cp /sdcard/app/leakcam_wake -> RAM: 0.58, 0.57, 0.21 s; RAM -> /sdcard: 3.33, 2.78, 0.81 s; read 24.4 MB/s, write 6.4 MB/s.


### Automated run 2026-10-30 (run.py, 63 s): pass

cp /sdcard/app/leakcam_wake -> RAM: 0.58, 0.58, 0.44 s; RAM -> /sdcard: 3.83, 3.22, 2.79 s; read 11.7 MB/s, write 1.9 MB/s.


### Automated run 2026-10-25 (run.py, 295 s): pass

cp /sdcard/app/leakcam_wake -> RAM: 0.49, 0.49, 0.36 s; RAM -> /sdcard: 3.31, 3.08, 2.50 s; read 14.4 MB/s, write 2.1 MB/s.

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)
- meter K230 copying RAM -> /sdcard over and over (cp): 0.259 A

### Run 2026-10-18: pass

Status: **pass** (2026-10-18).

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

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

#### Boot time (before the 2026-10-25 run)

Not recorded (before the boot rule, 2026-10-21). If re-run: the UFFS mount time of /sdcard and /bin, empty and full (a full /sdcard may mount slower: tree build scans blocks).

#### Battery energy (before the 2026-10-25 run)

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: extra current while reading and writing /sdcard vs idle; energy per MB written (clips) and read.

## Obstacles

- run 3: written file size 720896 != 5181000 (2026-11-11, automated run)

- **2026-11-11 23:16, FAIL: run 3 wrote 994888 of 5181000 B (own mistake, test030).** /sdcard was full: my camera runs
  had left cam0/cam1.nv12 (1.8 MB each) and cam0/cam1.h265 (4 MB each) on it. shot.py and stream.py now delete the
  board copy once the PC has it. **Correction (2026-11-12): not the cause.** Rerun with 19.5 MB free failed the same
  way; by hand 4 write + delete cycles all fine (19.5 -> 14.5 -> 19.5 MB, 2.8 s each: UFFS frees at once). Both short
  writes came 24-25 s after the boot = the agent's boot capture hook (`leakcam_wake cold`, 23-28 s, writes /sdcard):
  run.py now calls r.media_quiet() (stops the agent, waits for the hook) after the boot.

- run 3: written file size 994888 != 5181000 (2026-11-11, automated run)

- **`echo` cannot create files** -> msh `echo` opens without O_CREAT -> create with `cp` first.
- **No checksum tool on the K230** -> byte-exact check with `hexdump` of a 100 KB file, compared on the host.
- **Dump looked corrupted after boot** -> the agent's `READY not answered` lines interleave -> dump on a quiet console.
