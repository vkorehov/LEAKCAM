# test016: SPI NAND raw speed and SPI clock

Status: **planned** (2026-10-21).

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
