# test024: DDR stress (128 MB at 2667 MT/s)

Status: **in progress** (2026-10-21): memtest built into a test image.

SPL trains the DDR at 2667 MT/s on every boot (test006). A marginal setting shows only as rare random crashes.
This test runs a pattern memory test over most of the free RAM for a long time.

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


## Readings (2026-10-21)

| Time | State | Meter (USB-C 1 VBUS) | |
|---|---|---|---|
| 20:12:41-20:13:14 | test image flashed (`flash_nand.sh`: erase 8 s, write, 33 s in all) | - | |
| 20:13 | K230 rails on after the flash (USB loader idle), charge finished (D1 off) | 196 mA | |
| 20:14:24 | NAND boot (`pt_boot1 low`, `pt_on`, `pt_run`), console 20:14:31 | - | |
| 20:14:39 | `memtest 1`: 40 MB system RAM (4 MB left free) + 69 MB MMZ = 109 of 128 MB | - | 1 pass 6.5 s, 167 MB/s write+verify, 0 errors |
| 20:14:56 | `memtest 560` started (~1 h) | ask | |

## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| memtest MB/s under load, errors | not measured yet | DDR top (test015) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

Each DDR setting's training time (with test015), cold starts recorded.

## Battery energy

Battery current at 2667 and 3200 MT/s under the same load: the cost of the faster DDR per second, against the time it saves (test015, test021).

## Obstacles

- **"Memory exhaustion!" and `hil_mmb_alloc ... failed!` on the console at memtest start** -> memtest allocates
  until both allocators refuse; expected, not an error.
- **The long-run log stayed empty for 30 s** (my mistake) -> the one-pass run's `timeout 150 cat` was still reading
  the console (two readers split the data), and `tr` block-buffers into a pipe -> killed both by PID, restarted the
  logger with `stdbuf -oL` (scratchpad mtlog.sh); passes 1-4 are missing from the log, the run itself is unaffected.
- **Garbage + "U-Boot: command not found" at every boot** -> the SPL banner arrives while the agent prints, and the
  shell takes the banner's text as a command; harmless (seen since test021).

