# test025: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### 3200 MT/s leftovers, 2026-10-31 (scratch runner, 4 min): pass

5 cold starts (rails off 5 s) with memtest 3 passes after each: 89 MB x 5 patterns, 0 errors, 171-173 MB/s. Then the
KPU change net and the encoder (2 x 15 fps) started, memtest last: 46 MB (36 MB MMZ left), 3 passes, 0 errors. The
`hil_mmb_alloc(memtest, 1048576) failed` line is memtest reaching the end of the free MMZ (it takes 1 MB blocks until
none is left), not an error. Boot share: the SPL banner arrives together with the DDR "Firmware run has completed"
line at 0.158-0.160 s (5 boots): the SPL prints after training, the console cannot split it.

### Automated run 2026-10-31 (run.py, 77 s): pass

memtest: 9 pass lines seen, done with 0 errors in 61 s.


### Automated run 2026-10-25 (run.py, 77 s): pass

memtest: 10 pass lines seen, done with 0 errors in 61 s.

### Runs of 2026-10-23/24

**Run 3, DDR at 3200 MT/s (2026-10-23, top stable speed rule; SPL default 2667):**

| Setting | Training | Bandwidth (ddr_bench 3072) | memtest | |
|---|---|---|---|---|
| 2667 (before, test016) | pass | write 4.05, read 4.08, copy 1.86 GB/s | 0 errors | |
| 3200 HW-ODT (`K230D_SIP_LPDDR4_3200_HWODT`) | pass, app init 0.347 s | write **6.08**, read **4.83**, copy **2.77** GB/s | **kernel crash at its start**: Illegal Instruction in `rt_calloc` (0x17c2c2): fetched `0x8527`, the binary has `0x8526` (`mv a0,s1`): **one bit flipped** in kernel text in DDR | fail |
| 3200 all-ODT (`K230D_SIP_LPDDR4_3200_ALLODT`) | pass, app init 0.345 s | write 6.08, read 4.84, copy 2.77 GB/s (+50 / +18 / +49 %) | 3 passes 0 errors; **8 h soak 22:40:56 - 06:41:04: 5230 passes x 109 MB x 5 patterns (~570 GB written and verified), 0 errors, 0 resets**; 198 MB/s write+verify (167 at 2667) | **pass, adopted** |

8 h soak temperatures (476 readings, one a minute, no gap over 2 min: the VM never paused): K230 die 48.2 C at the
start, **60.5 C peak** (after ~2 h), 56.6-58.4 C later; board (AHT20) 36.4 -> **43.8 C peak**, 42.1 C at the end;
open board on the bench, room ~29 C. The agent's hook could not load leakcam_wake once at the start ("Memory
exhaustion": memtest holds the RAM), expected. Three temperature readings were the runner's parse mixing a memtest
line ("5.5 s") into chip_temp: no data.

**Pass at 3200 MT/s all-ODT** (with run 1 and the cold starts at 2667): 3200 all-ODT is the board's DDR setting from
now on (u-boot k230d_leakcam_defconfig). Still open: the same with the KPU and the encoder busy (step 3).

**Run 2 (2026-10-23 21:05:37-21:08:41, stopped by the user for the Wi-Fi work after 3 min; planned 1 h):** `memtest 0`
in the background, 30 passes, **0 errors**; die 43.4 -> 48.2 C, board (AHT20) 31.9 -> 35.9 C in 3 min, still rising.
The >= 1 h run stays open. Image: the test008 bench image (kdimg 17:47, memtest + six cpubench variants).

### Results (2026-10-21)

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
kernel init 0.207 s, mounts done 0.870 s, app init 0.893-0.894 s. (The CH340 console of these runs is replaced by /dev/ttyACM1 since test039.) The CH340 came back by itself 5-7 s after each
`pt_run` (no VMware click needed). Meter over the loop (boot + memtest): 205 mA average, 250 mA peak.

**Pass** for the cold starts; with run 1, 61 passes x 109 MB x 5 patterns, 0 errors, no reset.

### Superseded statements

- Status: **pass at 3200 MT/s** (2026-10-24: 8 h, 5230 passes, 0 errors; HW-ODT flipped a bit, all-ODT adopted). Earlier, 2667 (2026-10-21): 0 errors in 61 passes (5 min run + 5 cold starts). Open: the >= 1 h run and the run with KPU + encoder busy.
- Intro: "SPL trains the DDR at 2667 MT/s on every boot (test006)."

### Superseded metric values

- Boot, reset release -> app init (median of 5) (2026-10-21, 2667): 0.893 s (min 0.893, max 0.894); note: longest
  step: mounts /bin + /sdcard, 0.262 -> 0.870 s (+0.61 s); top/budget "test022: 0.88 s".
- Test run time (2026-10-21): run 1: 5 min 05 s (46 passes); setup: memtest written + image built 20:05-20:11, flash
  20:12:41-20:13:14, boot 20:14:24-31.
- memtest MB/s, errors (2026-10-21, 2667): 167 MB/s write+verify (5 patterns, MMZ uncached), 0 errors so far;
  note: 3 %: a checker, not a bandwidth test.

## Obstacles

(Dated 2026-10-23 first, then 2026-10-21 (undated bullets, from the run of that day), reversed within each day.)

- **That failed build exited 0** -> leakcam_docker_build.sh kept make's status only for exactly `log`; `log
  LEAKCAM_BENCH_DIR=...` fell through to the SDK's `make log` (tee, always 0) -> `log` with any arguments now runs
  under pipefail.
- **3200 MT/s did not build** (2026-10-23): its 2D-training init is generated by ddr_2d.sh, which calls `xxd`, missing in
  the build container -> `xxd` in firmware/k230_board/docker/Dockerfile (2667 uses ddr.sh, no xxd).
- **The agent's hook failed to load during memtest** (`[E/LWP] ERROR: ELF loading failed`, once per cold start) ->
  memtest leaves only 4 MB of system RAM free; the agent starts its hook ~10 s after boot -> expected while
  memtest runs; these are the only ERROR lines in the logs (memtest's own mismatch lines start with "ERROR pass").
- **`scripts/energy.py` (skill leakcam-test-metrics) does not exist** -> computed by hand (P = I x VBAT, E = P x t,
  Q = I x t) -> skill note corrected.
- **Garbage + "U-Boot: command not found" at every boot** -> the SPL banner arrives while the agent prints, and the
  shell takes the banner's text as a command; harmless (every boot on 2026-10-21).
- **The long-run log stayed empty for 30 s** (my mistake) -> the one-pass run's `timeout 150 cat` was still reading
  the console (two readers split the data), and `tr` block-buffers into a pipe -> killed both by PID, restarted the
  logger with `stdbuf -oL` (scratchpad mtlog.sh); passes 1-4 are missing from the log, the run itself is unaffected.
- **"Memory exhaustion!" and `hil_mmb_alloc ... failed!` on the console at memtest start** -> memtest allocates
  until both allocators refuse; expected, not an error.
