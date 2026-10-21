# test015: K230 DDR bandwidth

Status: **pass at 2667** (2026-10-21; 3200 not tried yet). Image: SDK copy + [leakcam_bench.c](../test016-nand-raw-speed/leakcam_bench.c).

DDR runs at 2667 MT/s (SPL training, test006), 64 MB. Bandwidth decides how much the KPU, the encoder and the
camera pipeline can do at once.

## Steps

1. A temporary MSH command (SDK copy, as the test014 bench; waits sleep, skill k230-rtsmart-hangs): `memcpy`,
   read-only (sum) and write-only (`memset`) over 1, 4, 16 MB blocks, cached; timed with `rdtime` (27 MHz).
2. Same through the uncached alias (DMA-visible), as the KPU/VENC see it.
3. Same while the KPU runs the change net in a loop (test012) and while the encoder runs (test026).

## Pass

Recorded MB/s per case; the drop under concurrent load; no errors (compare with test024's stress).


## Results (2026-10-21)

`ddr_bench 3072` (2 x 3 MB, larger than the caches), 60 runs in 12 s, 0 errors:

| | Measured | Spread over 60 runs |
|---|---|---|
| write (`memset`) | 4.05 GB/s | 4050-4057 |
| read (64-bit sum) | 4.06-4.09 GB/s | one outlier 3.76 |
| copy (`memcpy`, bytes moved; 2x traffic) | 1.86 GB/s | 1854-1868 |
| CPU clock (rdcycle / rdtime) | 1599-1600 MHz | top profile confirmed |

Not done yet: the uncached alias (step 2), 3200 MT/s (SPEEDS.txt 3, with test024).

## Obstacles

- **`ddr_bench 16384` crashed the kernel** (Load Page Fault, reboot) -> the 8 MB kernel heap has no 2 x 16 MB; the
  error path called `rt_free_align(NULL)`, which faults -> NULL guards in the bench (rebuilt with the next image);
  keep buffers <= 3 MB.

## Metrics (2026-10-21, bench image, 2667 MT/s)

Setup (2026-10-21): USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range, 0.1 mA
shown) in its VBUS; cell charged (D1 off). VBAT not measured: P uses 3.8 V nominal (`energy.py --vbat 3.8`).
Board floor: rails off, BL616 awake on USB 60.5 mA; rails on, K230 in reset 86.3 mA; K230 at msh idle (agent running,
READY not answered by the test firmware) 204 mA. A mA-range meter (2-5 ohm) reset the board; an earlier meter had a
burned mA fuse: readings from before the swap are void.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 1.00 s | test021 | longest step: UFFS mounts 0.70 s |
| This part's boot share | DDR training inside 0.23 s (ROM..OpenSBI) | - | not split further yet |
| Test run time | 12 s (60 runs) | - | setup: reflash 2 x (USB boot, 3 clicks) |
| DDR write / read / copy | 4.05 / 4.08 / 1.86 GB/s | 5.33 GB/s (2667 x 16 bit) | 76 % (write, read) |
| Current idle / during (min / avg / peak) | 204 / - / 270 mA | floor 204 (msh idle) | USB-C 1 VBUS |
| Power, energy per operation | +66 mA = +251 mW over idle; 18 mJ per 64 MB pass set | - | 3.8 V nominal |
| Per day | n/a (no DDR-only work in a session) | - | |

## Boot time

The DDR clock's effect on boot: training time at 2667 vs 3200 MT/s (SPL), from test021's timestamps.

## Battery energy

Battery current during each bench (copy, read, write) vs idle; energy per GB moved. With test024 at 3200 MT/s: does the faster clock cost more per GB or save by finishing sooner?
