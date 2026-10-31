# test016: K230 DDR bandwidth

Status: **pass at 3200 MT/s all-ODT** (2026-10-25): write 6.08 / read 4.84 / copy 2.77 GB/s, 8 x `ddr_bench 3072`
(run.py); uncached alias and concurrent load not measured yet.

Image: SDK copy + [leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c). DDR runs at 3200 MT/s all-ODT
(test025), 64 MB. Bandwidth decides how much the KPU, the encoder and the camera pipeline can do at once.

## Setup

USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range) in its VBUS; cell charged (D1 off).
K230 bench image (SDK copy + leakcam_bench.c), `ddr_bench <KB>` from msh.

## Needs (build features and dependencies)

Kernel only: rdtime/rdcycle, rt_malloc (bench in leakcam_bench.c); no drivers.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. A temporary MSH command (SDK copy, as the test015 bench; waits sleep, skill k230-rtsmart-hangs): `memcpy`,
   read-only (sum) and write-only (`memset`) over 1, 4, 16 MB blocks, cached; timed with `rdtime` (27 MHz).
2. Same through the uncached alias (DMA-visible), as the KPU/VENC see it.
3. Same while the KPU runs the change net in a loop (test013) and while the encoder runs (test027).

## Pass

Recorded MB/s per case; the drop under concurrent load; no errors (compare with test025's stress).

## Result

- At 3200 MT/s all-ODT: write (`memset`) 6.08 GB/s, read (64-bit sum) 4.84 GB/s, copy (`memcpy`, bytes moved) 2.77
  GB/s, `ddr_bench 3072` (2 x 3 MB, larger than the caches), 8 runs, 0 errors (run.py, 2026-10-25; same as test025
  2026-10-23). Against 2667 MT/s (2026-10-21): write +50 %, read +18 %, copy +49 %.
- CPU clock 1599-1600 MHz (rdcycle / rdtime): top profile (2026-10-21).
- 3200 HW-ODT gives the same figures but flipped a bit; all-ODT is the clean setting (test025, 2026-10-23).
- The 8 MB kernel heap has no 2 x 16 MB: keep bench buffers <= 3 MB (2026-10-21).

Open:
- Step 2 (uncached alias, as the KPU/VENC see it) not done.
- Step 3 (under KPU / encoder load) not done.
- Current and energy at 3200 MT/s: not measured yet (the 2026-10-21 figures were at 2667 MT/s, superseded).

## Metrics (bench image; date per value)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | pt_run -> msh 0.52-0.53 s at 3200 MT/s (2026-10-30, test017's 20 boots); boot timing: test022 | test022 | longest step: UFFS mounts 0.70 s |
| This part's boot share | DDR training inside 0.23 s (ROM..OpenSBI) | - | not split further yet |
| Test run time | 82 s automated (run.py, 8 x ddr_bench 3072) (2026-10-25) | - | setup: reflash (USB boot) |
| DDR write / read / copy | 6.08 / 4.84 / 2.77 GB/s at 3200 MT/s all-ODT (2026-10-25) | - | |
| Current idle / during (min / avg / peak) | idle 0.220 (test006) / ddr_bench 3072 over and over 0.250 average / 0.303 peak A at 5 V (USB-C 1 VBUS meter, the user, 2026-10-31) | floor 204 (msh idle) | USB-C 1 VBUS |
| Voltage under load (min) | VBAT 4.31 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 250 mA x 4.33 V = 1083 mW; per ddr_bench 3072 (host-timed) (0.21 s) 0.23 J, 15 uAh; over idle (220 mA) 0.03 J (2026-10-31) | - | 3.8 V nominal |
| Per day | n/a (no DDR-only work in a session) | - | |
| Temperature die / board (start / peak / end) | die 46.4 / 48.5 / 48.5 C; board 37.3 / 37.4 / 37.4 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 / 7023232 / 7312064 B; BL616 no leak (mem_leak.py, 9 snapshots); K230 no leak (mem_leak.py, 9 snapshots) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | kernel test commands in leakcam_bench.o: 11.2 KB code + 0.6 KB RAM for all 13 (ddr/nand/uart_bench, boot_times, plt_*, rtc_*, pad_read, bat_watch); no init, 0 ms boot; not in the product image (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (DDR bandwidth; memcpy/memset are the C library's): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

The DDR clock's effect on boot: training time at 2667 vs 3200 MT/s (SPL), from test022's timestamps.

## Battery energy

Current (USB-C 1 meter) during each bench (copy, read, write) vs idle; energy per GB moved. With test025 at 3200 MT/s: does the faster clock cost more per GB or save by finishing sooner?
