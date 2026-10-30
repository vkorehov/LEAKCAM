# test017: SPI NAND raw speed and SPI clock

Status: **pass** (2026-10-30): NAND at 50 MHz, sample delay 5 (kernel + U-Boot/SPL), for stability; raw read 15.7 MB/s,
20/20 cold boots clean with the Wi-Fi bridge up, 0 ID retries.

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

- Raw MTD, 2 KiB pages, quad SPI: read 15.6-15.7 MB/s at 50 MHz; program 4.6-5.0 MB/s,
  erase 2.46 ms / 128 KiB at every clock (chip-limited, W25N02KV); reads scale with the clock (2026-10-21).
- Setting: **50 MHz, RX sample delay 5** (window 3-8), kernel `CONFIG_SPI_NAND_SPI_FREQ=50000000`,
  `CONFIG_RT_SPI0_RXSD=5`; U-Boot/SPL dts `spi-max-frequency = <50000000>`, `rx-sample-delay-ns = <6>` (6 / 1.25 = 5)
  (2026-10-30, user: stability over speed).
- Why not faster: at 80 MHz the kernel's first READ ID after reset came back one byte late (`unknown raw ID aa220000`,
  Winbond's ID is ef aa 22) in 2 of 32 cold boots; the kernel read it once and gave up, so /bin and /sdcard stayed
  unmounted. Data reads and checksums were always right: a marginal first transfer, not bit errors. 1 in ~20, so the
  earlier 18- and 20-boot checks passed by chance. Details: SPEEDS.txt, HISTORY.md.
- 50 MHz: 20/20 cold boots with the Wi-Fi bridge up and joined, 0 ID retries; 8 x `nand_bench read 64` 15.7 MB/s,
  checksums equal (2026-10-30, run.py).
- The kernel resets and re-reads the ID up to 3 times and logs `ID retry <n>` (install.sh, 2026-10-30): one bad read no
  longer costs the boot's file systems.
- U-Boot's RX sample delay step is computed in ps (Canaan's whole-ns step gave delay 6 for 6 ns at 800 MHz, the
  kernel uses 5) (kept 2026-10-25).
- SPI0 (OSPI, DW SSI): ssi_clk 800 MHz, SCLK = 800 / BAUDR (50 MHz = /16);
  RX_SAMPLE_DLY step 1.25 ns (2026-10-21).
- Faster clocks and the per-phase timing tried before: HISTORY.md and SPEEDS.txt.

Open:
- Raw program speed at 50 MHz not re-measured (chip-limited before: 4.6-5.0 MB/s).

## Metrics (2026-10-21, bench image)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | pt_run -> msh 0.52-0.53 s, mounts done 0.51-0.52 s (20 cold boots, 2026-10-30); reset release -> app init 0.403 s (test022, 8 boots) | test022 | UFFS mounts: NAND reads, the clock matters |
| This part's boot share | the mounts: +0.06 s against the faster clock tried before (2026-10-30) | - | |
| Test run time | 54 s automated (run.py, 2026-10-25), no setup | - | |
| Raw read | 15.7 MB/s (8 runs, 2026-10-30) | 25 MB/s bus at 50 MHz | 63 % |
| Raw read at faster clocks | n/a: dropped for stability (Result, SPEEDS.txt) | - | |
| Program | 4.6-5.0 MB/s | chip limit | |
| Current idle / read / program | 220 / 225 mA (read at 50 MHz, 2026-10-21); program 235-236 mA (2026-10-21) | floor 204 | USB-C 1 VBUS |
| Voltage under load (min) | VBAT 4.34 V min (K230 ADC ch 1, test023 calibration, 9 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Energy per MiB read, whole board | 57 mJ (VBAT 3.8 V nominal, 2026-10-21) | - | |
| Per day | inside the boot (mounts) and sessions | ENERGY.txt 3 | |
| Temperature die / board (start / peak / end) | die 42.8 / 43.7 / 43.1 C, board (AHT20) 33.4 / 33.9 / 33.9 C (2026-10-24, 10 /bin reads + writes) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | K230 heap free 7.03 / 7.03 / 7.31 MB of 8 MiB, max used 1.53 MB; BL616 free 230960 / min 226748 / 230960 B, 14 blocks, 0 deleted tasks, unchanged over 10 reads: no leak (2026-10-24) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | n/a: the NAND driver is the boot device, part of test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (NAND QSPI bus at 50 MHz): compiler flags do not change the result (2026-10-25) | the faster one stays | |
| Power, energy per operation | 222 mA x 4.34 V = 963 mW; per 8 MB raw read (64 blocks) (0.41 s) 0.39 J, 25 uAh; over idle (220 mA) 0.00 J (2026-10-25) | - |  |

## Boot time

The NAND clock's effect on boot: at 50 MHz pt_run -> msh 0.52-0.53 s, 0.06 s more than the faster clock dropped for
stability (2026-10-30); app init 0.403 s after reset release (test022).

## Battery energy

Read at 50 MHz: 225 mA at 5 V in, 57 mJ per MiB read (2026-10-21). Program / erase energy per MB not measured.
