# test016: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-31 (run.py, 144 s): pass

8 x `ddr_bench 3072`; last run:

    copy (memcpy, bytes moved)      64512 KB in  23836 us: 2771.4 MB/s
    write (memset)                  64512 KB in  10847 us: 6090.1 MB/s
    read (64-bit sum)               64512 KB in  13659 us: 4836.3 MB/s
    copy (memcpy, bytes moved)      64512 KB in  23834 us: 2771.6 MB/s

- meter K230 running ddr_bench 3072 over and over: 0.250 A


### Automated run 2026-10-31 (run.py, 52 s): pass

8 x `ddr_bench 3072`; last run:

    copy (memcpy, bytes moved)      64512 KB in  23821 us: 2773.1 MB/s
    write (memset)                  64512 KB in  10868 us: 6078.4 MB/s
    read (64-bit sum)               64512 KB in  13669 us: 4832.8 MB/s
    copy (memcpy, bytes moved)      64512 KB in  23812 us: 2774.2 MB/s


### Status line before the 2026-10-29 cleanup

Status: **pass at 3200** (2026-10-23, all-ODT: write 6.08 / read 4.84 / copy 2.77 GB/s; 2667 before: 4.05 / 4.08 / 1.86). Image: SDK copy + [leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c).

### Automated run 2026-10-25 (run.py, 82 s): pass

8 x `ddr_bench 3072`; last run:

    copy (memcpy, bytes moved)      64512 KB in  23841 us: 2770.8 MB/s
    write (memset)                  64512 KB in  10860 us: 6082.8 MB/s
    read (64-bit sum)               64512 KB in  13655 us: 4837.8 MB/s
    copy (memcpy, bytes moved)      64512 KB in  23831 us: 2772.0 MB/s

### At 3200 MT/s all-ODT (2026-10-23, test025)

**At 3200 MT/s all-ODT (2026-10-23, test025):** write 6.08 GB/s (+50 %), read 4.84 (+18 %), copy 2.77 (+49 %);
3200 HW-ODT gave the same figures but flipped a bit (test025). Not done yet: the uncached alias (step 2).

### Results (2026-10-21, 2667 MT/s)

`ddr_bench 3072` (2 x 3 MB, larger than the caches), 60 runs in 12 s, 0 errors:

| | Measured | Spread over 60 runs |
|---|---|---|
| write (`memset`) | 4.05 GB/s | 4050-4057 |
| read (64-bit sum) | 4.06-4.09 GB/s | one outlier 3.76 |
| copy (`memcpy`, bytes moved; 2x traffic) | 1.86 GB/s | 1854-1868 |
| CPU clock (rdcycle / rdtime) | 1599-1600 MHz | top profile confirmed |

### Superseded text

- Purpose (2026-10-21): DDR runs at 2667 MT/s (SPL training, test006), 64 MB.
- Metrics heading: ## Metrics (2026-10-21, bench image, 2667 MT/s)

### Metrics setup (2026-10-21, bench image)

Setup (2026-10-21): USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range, 0.1 mA
shown) in its VBUS; cell charged (D1 off). VBAT not measured: P uses 3.8 V nominal (`energy.py --vbat 3.8`).
Board floor: rails off, BL616 awake on USB 60.5 mA; rails on, K230 in reset 86.3 mA; K230 at msh idle (agent running,
READY not answered by the test firmware) 204 mA. A mA-range meter (2-5 ohm) reset the board; an earlier meter had a
burned mA fuse: readings from before the swap are void.

### Superseded metric values

- 2026-10-21 (2667 MT/s): DDR write / read / copy 4.05 / 4.08 / 1.86 GB/s; top 5.33 GB/s (2667 x 16 bit), 76 %
  (write, read).
- 2026-10-21: Test run time 12 s (60 runs); setup: reflash 2 x (USB boot, 3 clicks).

## Obstacles

- **`ddr_bench 16384` crashed the kernel** (Load Page Fault, reboot) -> the 8 MB kernel heap has no 2 x 16 MB; the
  error path called `rt_free_align(NULL)`, which faults -> NULL guards in the bench (rebuilt with the next image);
  keep buffers <= 3 MB.
