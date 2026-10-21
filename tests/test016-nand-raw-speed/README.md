# test016: SPI NAND raw speed and SPI clock

Status: **pass at 50 and 80 MHz, 100 MHz fails** (2026-10-21). Bench: [leakcam_bench.c](leakcam_bench.c) (SDK copy, `bsp/maix3/board/`).

test010 measured the file system: read 9.4 MB/s (shell) / 12.7 MB/s (user program), write 1.8 MB/s. This splits
it into the NAND itself and UFFS, and checks the SPI clock's headroom.

## Steps

1. Raw MTD read and program+erase of a spare area (not /bin, /sdcard): page, 64 KB, 1 MB transfers; timed.
2. UFFS on the same sizes (test010 method) for the overhead.
3. The SPI controller clock and mode in use (quad, DTR?) from the driver; if it runs below the W25N02KV's limit, raise
   it one step at a time with a read-verify of the whole image area, as test014 did for SDIO.
4. Boot impact: the SPL/U-Boot NAND read speed (part of test101's boot time).

## Pass

Raw vs file system figures recorded; the fastest clock that verifies over the whole image area 10 times is the
setting, or the current one is shown to be the limit.


## Results (2026-10-21)

Raw MTD, 2 KiB pages, quad SPI; read on nand0 (/bin, blocks 384..), program/erase on the free blocks 1984..2047:

| NAND clock | Read | Program | Erase / 128 KiB | Read back | Check |
|---|---|---|---|---|---|
| 50 MHz (now) | 15.6 MB/s | 4.6 MB/s | 2.46 ms | 14.7 MB/s | checksums equal over 20 runs, 0 errors |
| 66 MHz | 18.6 MB/s | 4.8 MB/s | 2.46 ms | 17.3 MB/s | pass |
| **80 MHz** | **20.6 MB/s** | **5.0 MB/s** | 2.46 ms | 18.9 MB/s | **10 x whole /bin (20 MiB) = same checksum, 3 writes verified** |
| 100 MHz | zeros (36 MB/s "fast") | fails | "31 us" (never ran) | - | **fail**: data all 0, driver reports 0 errors |

After the 100 MHz run, back at 50 MHz: checksums unchanged, 0 false bad blocks in the spare area, /bin and /sdcard
list normally: no damage. Program and erase are chip-limited (W25N02KV); reads scale with the clock.

Not done yet for adopting 80 MHz (SPEEDS.txt 3): SPL and U-Boot at 80 too, 10 cold boots, the /sdcard area; 100 MHz
might need the controller's RX sample delay (to look at, not a board fault: 80 is clean).

## Obstacles

- **100 MHz returns zeros without an error** -> the driver has no read check -> every clock step needs a checksum
  against a known read, never the driver's status.

## Metrics (2026-10-21, bench image)

Setup (2026-10-21): USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range, 0.1 mA
shown) in its VBUS; cell charged (D1 off). VBAT not measured: P uses 3.8 V nominal (`energy.py --vbat 3.8`).
Board floor: rails off, BL616 awake on USB 60.5 mA; rails on, K230 in reset 86.3 mA; K230 at msh idle (agent running,
READY not answered by the test firmware) 204 mA. A mA-range meter (2-5 ohm) reset the board; an earlier meter had a
burned mA fuse: readings from before the swap are void.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 1.00 s | test021 | UFFS mounts 0.70 s: NAND reads, the clock matters |
| This part's boot share | the mounts, 0.70 s (70 %) | - | at 50 MHz |
| Test run time | read loop 12 s, write loop 21 s, clock steps 10 s, stability 30 s | - | |
| Raw read 50 / 80 MHz | 15.6 / 20.6 MB/s | 25 / 40 MB/s bus | 62 % / 52 % |
| Program | 4.6-5.0 MB/s | chip limit | |
| Current idle / read 50 / read 80 / program | 204 / 225 / 232 / 235-236 mA | floor 204 | USB-C 1 VBUS |
| Energy per MiB read, whole board | 57 mJ at 50 MHz, 45 mJ at 80 MHz | - | 21 % less: shorter time at the 204 mA floor |
| Per day | inside the boot (mounts) and sessions | ENERGY.txt 3 | |

## Boot time

The NAND clock's effect on boot: SPL kernel load and UFFS mount time at each clock (test021 milestones).

## Battery energy

Battery current during raw reads, programs, erases at each NAND clock; energy per MB read and per MB written, and how it changes with the clock (faster clock, shorter on-time).
