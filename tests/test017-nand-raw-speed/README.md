# test017: SPI NAND raw speed and SPI clock

Status: **pass** (2026-10-25): NAND at 80 MHz, sample delay 5 (kernel + U-Boot/SPL); raw read 20.6 MB/s, 20/20 cold boots clean.

test011 measured the file system: read 14.4 MB/s (shell) / 12.7 MB/s (user program), write 2.1 MB/s. This splits
it into the NAND itself and UFFS, and checks the SPI clock's headroom. Bench: [leakcam_bench.c](leakcam_bench.c)
(SDK copy, `bsp/maix3/board/`).

## Setup

USB-C 1 on the Mac (power + K230 USB boot), BL616 test firmware (bl616_powertest) on /dev/ttyACM0,
K230 console on /dev/ttyACM1 (tools/power-bringup/board.py); RPi camera unplugged (bugs/bug-005). Image:
k230d_rtos_leakcam kdimg with the bench (leakcam_bench.c in the SDK's board/), flashed hands-free (BOOT1 high through
the BL616). Probes: as in Metrics.

## Needs (build features and dependencies)

SPI NAND driver (spinand, QSPI on SPI0, RT_SPI0_RXSD), MTD; bench in leakcam_bench.c.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Raw MTD read and program+erase of a spare area (not /bin, /sdcard): page, 64 KB, 1 MB transfers; timed.
2. UFFS on the same sizes (test011 method) for the overhead.
3. The SPI controller clock and mode in use (quad, DTR?) from the driver; if it runs below the W25N02KV's limit, raise
   it one step at a time with a read-verify of the whole image area, as test015 did for SDIO.
4. Boot impact: the SPL/U-Boot NAND read speed (part of test101's boot time).

## Pass

Raw vs file system figures recorded; the fastest clock that verifies over the whole image area 10 times is the
setting, or the current one is shown to be the limit.

## Result

- Raw MTD, 2 KiB pages, quad SPI: read 15.6 MB/s at 50 MHz, 18.6 at 66 MHz, 20.6 at 80 MHz; program 4.6-5.0 MB/s,
  erase 2.46 ms / 128 KiB at every clock (chip-limited, W25N02KV); reads scale with the clock (2026-10-21).
- Setting: **80 MHz, RX sample delay 5** (window 3-8 at both 50 and 80 MHz): kernel
  `CONFIG_SPI_NAND_SPI_FREQ=80000000`, `CONFIG_RT_SPI0_RXSD=5`; U-Boot/SPL dts `spi-max-frequency = <80000000>`,
  `rx-sample-delay-ns = <6>` (6 ns / 1.25 = 5). 10 x whole /bin (20 MiB) same checksum, 3 writes verified (2026-10-21).
- 80 MHz with the current image: 20/20 cold boots clean, /bin read correct every time (2026-10-25); 8 x
  `nand_bench read 64` 20.6 MB/s, checksums equal (2026-10-25, run.py).
- U-Boot's RX sample delay step is computed in ps (Canaan's whole-ns step gave delay 6 for 6 ns at 800 MHz, the
  kernel uses 5) (kept 2026-10-25).
- SPI0 (OSPI, DW SSI): ssi_clk 800 MHz, SCLK = 800 / BAUDR (50 = /16, 66.7 = /12, 80 = /10, 100 = /8);
  RX_SAMPLE_DLY step 1.25 ns (2026-10-21).
- 100 MHz single timing fails at every sample delay and pad drive (ds 7, 10, 15): zeros with "0 errors" or ECC
  errors; bus errors are silent (ECC inside the chip), so every clock change is checked by checksum (2026-10-21).
- 100 MHz data with 80 MHz commands (per-phase driver patch) read correctly (23.0 MB/s, +12 %), but 2 cold boots in 70
  lost the NAND ID with the patched SPL; dropped by the user, patches removed (2026-10-25). Untried: the W25N02KV's own
  output driver strength.

Open:
- Boot time and mount time at 80 MHz with the current image not recorded since the 2026-10-25 cleanup.

## Metrics (2026-10-21, bench image)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 0.876-0.886 s at 80 MHz (11 cold boots read, bench image) (2026-10-21) | test022 | UFFS mounts: NAND reads, the clock matters |
| This part's boot share | the mounts, 0.59 s (2026-10-21, the 100 MHz analysis) | - | |
| Test run time | 54 s automated (run.py, 2026-10-25), no setup | - | |
| Raw read 50 / 80 MHz | 15.6 / 20.6 MB/s | 25 / 40 MB/s bus | 62 % / 52 % |
| Raw read, data at 100 MHz | n/a: 100 MHz dropped 2026-10-25 (user); 2026-10-24 figures in HISTORY.md | 50 MB/s bus | |
| Program | 4.6-5.0 MB/s | chip limit | |
| Current idle / read 80 / program | 220 / 222 mA (2026-10-25, run.py); program 235-236 mA (2026-10-21) | floor 204 | USB-C 1 VBUS |
| Voltage under load (min) | VBAT 4.34 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Energy per MiB read, whole board | 57 mJ at 50 MHz, 45 mJ at 80 MHz (VBAT 3.8 V nominal, 2026-10-21) | - | 21 % less: shorter time at the 204 mA floor |
| Per day | inside the boot (mounts) and sessions | ENERGY.txt 3 | |
| Temperature die / board (start / peak / end) | die 42.8 / 43.7 / 43.1 C, board (AHT20) 33.4 / 33.9 / 33.9 C (2026-10-24, 10 /bin reads + writes) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | K230 heap free 7.03 / 7.03 / 7.31 MB of 8 MiB, max used 1.53 MB; BL616 free 230960 / min 226748 / 230960 B, 14 blocks, 0 deleted tasks, unchanged over 10 reads: no leak (2026-10-24) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | n/a: the NAND driver is the boot device, part of test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (NAND QSPI bus at 80 MHz): compiler flags do not change the result (2026-10-25) | the faster one stays | |
| Power, energy per operation | 222 mA x 4.34 V = 963 mW; per 8 MB raw read (64 blocks) (0.41 s) 0.39 J, 25 uAh; over idle (220 mA) 0.00 J (2026-10-25) | - |  |

## Boot time

The NAND clock's effect on boot: at 50 MHz app init 1.00 s with the UFFS mounts 0.70 s; at 80 MHz app init
0.876-0.886 s (11 cold boots), mounts 0.59 s (2026-10-21, bench image; the 100 MHz analysis).

## Battery energy

Read at 80 MHz: 222 mA x 4.34 V = 963 mW, 0.39 J per 8 MB raw read, nothing measurable over idle (2026-10-25); per
MiB read 45 mJ at 80 MHz against 57 mJ at 50 MHz (2026-10-21). Program / erase energy per MB not measured.
