# test022: K230 boot time

Status: **0.32 s** (2026-10-25): reset release -> app init 0.320 / 0.320 / 0.321 s (min / median / max, 8 of 8 cold
boots, run.py); msh prompt and agent READY not timed yet.

Every wake powers the K230 up from cold, so its boot time is paid on every session (the whole session: test101).
The design wants the fastest boot: it is paid on every wake (ENERGY.txt, the biggest lever). Target to set from the
measurements; first candidates: sensor probes without cameras, the /sdcard UFFS mount, DDR training, the kernel load
(NAND clock).

## Setup

BL616 test firmware; K230 NAND image (current test image or the product image); both consoles recorded with
timestamps (skill k230-rtsmart-hangs `console_record.py`, K230 console `/dev/ttyACM1` and BL616 `/dev/ttyACM0`, both on PR1:
since test039 the K230 console runs over the BL616 link from the first SPL line and stays up across cold boots).

The kernel times itself: `rdtime` (27 MHz) counts from the K230's reset release (a crash reboot restarts it too), and
temporary init hooks record it at the end of each RT-Thread init level (`boot_times`,
[leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c)).

## Needs (build features and dependencies)

Kernel init levels, UFFS mounts (dominant), boot_times (leakcam_bench.c); SPL/U-Boot NAND clock.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Rails off >= 5 s, then `pt_on` (the BL616 prints the 3V3 ramp), `pt_run` at t0 (reset released).
2. From the K230 console, the time of each milestone after t0:
   - U-Boot SPL banner (ROM read SPL from NAND);
   - "Firmware run has completed 2667" (DDR training done);
   - OpenSBI banner;
   - RT-Smart banner;
   - UFFS mounted (/bin, /sdcard), `msh />`;
   - agent `READY` on the link (the BL616's UART0 shows it with `pt_uart on`).
3. 5 cold boots: median and spread per step; name the longest step.
4. Repeat after each speed change that touches boot (NAND clock test017, DDR 3200 test025) to see what it bought.

## Pass

Recorded per step; the biggest steps named as candidates to shorten.

## Result

- Reset release -> app init **0.320 s** median (0.320-0.321 s, 8 of 8 cold boots), UFFS mounts done at
  0.311-0.312 s (run.py, 2026-10-25); test039 measured 0.296-0.301 s app init over 3 cold boots (2026-10-23) with the
  console at 4 Mbaud from SPL.
- UFFS mount time goes with the **used pages**, not the partition size: it reads the tags of every page holding data
  (~65 us per page) (2026-10-21, /sdcard and /bin resize runs). Levers: less data (ISP tuning files for one
  resolution only, stripped programs) or /sdcard mounted only when a session needs it.
- NAND at 80 MHz in the boot (100 MHz has no window, test017); /bin holds only the 1280x960 ISP set (0.68 MB),
  /sdcard 32 MiB, /bin 8 MiB (2026-10-21).

Open:
- main/msh and the agent's READY not timed (needs the link protocol in the BL616 test firmware, or a K230-side hook).
- Energy per boot with today's 0.32 s boot not measured: the meter's average over a loop of cold boots (no scope,
  user 2026-10-30); the figure below still uses the 0.88 s boot of 2026-10-21.

## Metrics (bench image; date per value)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | 0.320 / 0.321 s (median / max of 8 cold boots; mounts done 0.311-0.312 s) (2026-10-25, run.py) | target: to set | longest step: UFFS mounts |
| This part's boot share | - | - | (the boot is this test) |
| Test run time | 1 command; setup: reflash (USB boot) ~5 min with clicks | - | |
| NAND clock in the boot | 80 MHz | 100 MHz has no window (test017) | |
| Current: rails in reset / boot peak / msh idle | 86.3 / ~227 / 204 mA (2026-10-21) | - | USB-C 1 VBUS |
| Voltage under load (min) | VBAT 4.34 V min (K230 ADC ch 1, test023 calibration, 8 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per boot (to app init) | ~215 mA x 0.88 s = 0.72 J, 53 uAh (2026-10-21, at the 0.88 s boot; not recomputed for 0.32 s) | - | 3.8 V nominal |
| Per day | 4 report sessions: 0.24 mAh/day (boot to app init only) (2026-10-21, at the 0.88 s boot) | ENERGY.txt 3 | 0.7 % of 1000 mAh / month |
| Temperature die / board (start / peak / end) | die 34.0 / 37.6 / 37.6 C; board 28.3 / 30.1 / 30.1 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; K230 heap available 7025152 / 7025152 / 7025152 B (max used 1390848); BL616 no leak: free 208608 -> 208608 B (slope +0 B per cycle) over 6 cycles (mem_leak.py, 8 snapshots); K230 no leak: free 7025152 -> 7025152 B (slope +0 B per cycle) over 6 cycles (mem_leak.py, 8 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 45.4 MB (/sdcard 32 MiB, /bin 8 MiB one ISP set), kernel 3.75 MB, /bin 0.68 MB, /sdcard programs 6.76 MB (latest run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | kernel test commands in leakcam_bench.o: 11.2 KB code + 0.6 KB RAM for all 13 (ddr/nand/uart_bench, boot_times, plt_*, rtc_*, pad_read, bat_watch); no init, 0 ms boot; not in the product image (2026-10-25); boot_times reads rdtime at each init level (no cost) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | kernel stays -O1: -O2 faults in the LWP signal return (test008 Flags A, 2026-10-22), kept -O1 by the user's decision; this result is kernel/driver code, no faster build to compare until that is fixed (2026-10-25) | the faster one stays | |

## Boot time

This test is the boot time measurement.

## Battery energy

Current through the boot: the USB-C 1 meter's average over a loop of cold boots times the boot time (no scope, user
2026-10-30; no per-step split): energy per boot = the fixed cost of every wake. Also after each speed change (NAND clock, DDR 3200).
