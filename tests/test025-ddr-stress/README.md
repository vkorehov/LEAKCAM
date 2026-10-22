# test025: DDR stress (128 MB at 2667 MT/s)

Status: **pass so far** (2026-10-21): 0 errors in 61 passes (5 min run + 5 cold starts). Open: the >= 1 h run and the run with KPU + encoder busy.

SPL trains the DDR at 2667 MT/s on every boot (test006). A marginal setting shows only as rare random crashes.
This test runs a pattern memory test over most of the free RAM for a long time.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

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

## Results (2026-10-21)

| Time | State | Meter (USB-C 1 VBUS) | |
|---|---|---|---|
| 20:12:41-20:13:14 | test image flashed (`flash_nand.sh`: erase 8 s, write, 33 s in all) | - | |
| 20:13 | K230 rails on after the flash (USB loader idle), charge finished (D1 off) | 196 mA | |
| 20:14:24 | NAND boot (`pt_boot1 low`, `pt_on`, `pt_run`), console 20:14:31 | - | |
| 20:14:39 | `memtest 1`: 40 MB system RAM (4 MB left free) + 69 MB MMZ = 109 of 128 MB | - | 1 pass 6.5 s, 167 MB/s write+verify, 0 errors |
| 20:14:56 | `memtest 560` started (~1 h) | - | |
| 20:17 | memtest running (pass ~20, 0 errors) | 240 mA, short peaks to 260 mA | msh idle 202 mA (test023): memtest +38 mA; the peaks are too short for the meter (pass phases, likely the uncached MMZ writes) |
| 20:18 | memtest running | 260 mA | up from 240 mA: warming up (AHT20 37 C at 19:51) or a heavier phase; watch the trend |

**Run 1 (20:14:56-20:20:01), cut short by the user after 46 passes (5 min; planned 560, ~1 h):** 46 x 109 MB x 5
patterns, **0 errors**, no reset, 6.5 s per pass throughout. A >= 1 h run stays open for a later session.

**Cold starts (20:21-20:24):** 5 x (`pt_off`, 3 s, `pt_on`, `pt_run`: DDR retrained each time), then `boot_times` and
`memtest 3`. Every start: 3 passes, **0 errors**, 6.5-6.6 s per pass. Boot identical every time (from reset release):
kernel init 0.207 s, mounts done 0.870 s, app init 0.893-0.894 s. The CH340 came back by itself 5-7 s after each
`pt_run` (no VMware click needed). Meter over the loop (boot + memtest): 205 mA average, 250 mA peak.

**Pass** for the cold starts; with run 1, 61 passes x 109 MB x 5 patterns, 0 errors, no reset.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init (median of 5) | 0.893 s (min 0.893, max 0.894) | test022: 0.88 s | longest step: mounts /bin + /sdcard, 0.262 -> 0.870 s (+0.61 s) |
| This part's boot share | 0.207 s before kernel init includes SPL's DDR training (not split out) | - | |
| Test run time | run 1: 5 min 05 s (46 passes) | - | setup: memtest written + image built 20:05-20:11, flash 20:12:41-20:13:14, boot 20:14:24-31 |
| memtest MB/s, errors | 167 MB/s write+verify (5 patterns, MMZ uncached), 0 errors so far | DDR top 5.33 GB/s (test016) | 3 %: a checker, not a bandwidth test |
| Current idle / during | 196 mA (rails on after flash) / 202 mA msh (test023) / 240 mA memtest, peaks 260 mA | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT ~4.15 V (full cell) |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 1.0 W during memtest (240 mA x 4.15 V), +158 mW over msh idle; per pass (6.5 s) 6.5 J / 433 uAh, +1.0 J / 69 uAh over idle | - | by hand: scripts/energy.py does not exist |
| Per day | n/a: a bench test, not in the product | - | |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, --gc-sections (no -ffunction-sections, no LTO) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

Each DDR setting's training time (with test016), cold starts recorded.

## Battery energy

Battery current at 2667 and 3200 MT/s under the same load: the cost of the faster DDR per second, against the time it saves (test016, test022).

## Obstacles

- **"Memory exhaustion!" and `hil_mmb_alloc ... failed!` on the console at memtest start** -> memtest allocates
  until both allocators refuse; expected, not an error.
- **The long-run log stayed empty for 30 s** (my mistake) -> the one-pass run's `timeout 150 cat` was still reading
  the console (two readers split the data), and `tr` block-buffers into a pipe -> killed both by PID, restarted the
  logger with `stdbuf -oL` (scratchpad mtlog.sh); passes 1-4 are missing from the log, the run itself is unaffected.
- **Garbage + "U-Boot: command not found" at every boot** -> the SPL banner arrives while the agent prints, and the
  shell takes the banner's text as a command; harmless (every boot on 2026-10-21).
- **`scripts/energy.py` (skill leakcam-test-metrics) does not exist** -> computed by hand (P = I x VBAT, E = P x t,
  Q = I x t) -> skill note corrected.
- **The agent's hook failed to load during memtest** (`[E/LWP] ERROR: ELF loading failed`, once per cold start) ->
  memtest leaves only 4 MB of system RAM free; the agent starts its hook ~10 s after boot -> expected while
  memtest runs; these are the only ERROR lines in the logs (memtest's own mismatch lines start with "ERROR pass").
