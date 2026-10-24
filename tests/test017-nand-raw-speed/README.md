# test017: SPI NAND raw speed and SPI clock

Status: **100 MHz data adopted** (2026-10-24, kernel): 1-line transfers (commands, status polls) at 80 MHz delay 5,
4-line data at 100 MHz delay 6 + edge (per-phase SPI timing patch in install.sh); U-Boot/SPL stay at 80 MHz delay 5. Bench: [leakcam_bench.c](leakcam_bench.c) (SDK copy, `bsp/maix3/board/`).

test011 measured the file system: read 9.4 MB/s (shell) / 12.7 MB/s (user program), write 1.8 MB/s. This splits
it into the NAND itself and UFFS, and checks the SPI clock's headroom.

## Setup

2026-10-24: USB-C 1 on the Mac (power + K230 USB boot), BL616 test firmware (bl616_powertest) on /dev/ttyACM0,
K230 console on /dev/ttyACM1 (tools/power-bringup/board.py); RPi camera unplugged (Obstacles). Image: k230d_rtos_leakcam
kdimg with the bench (leakcam_bench.c in the SDK's board/), flashed hands-free (BOOT1 high through the BL616).
Probes: as in Metrics; no current meter in the 2026-10-24 runs.

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

### Why 100 MHz fails: the read sample delay (2026-10-21)

The NAND is on SPI0 (OSPI, DW SSI): ssi_clk **800 MHz**, SCLK = 800 / BAUDR (50 = /16, 66.7 = /12, 80 = /10,
100 = /8). RX_SAMPLE_DLY delays sampling by n x 1.25 ns (boot default `RT_SPI0_RXSD` 6). Read-only sweep
(`nand_clk`, `nand_rxsd n`, `nand_bench read 64`, reference checksum from 50 MHz):

| Delay | 50 MHz | 80 MHz | 100 MHz |
|---|---|---|---|
| 0 | 1 bit flipped, 0 errors | zeros | ECC errors |
| 1-2 | clean | zeros | ECC errors |
| 3 | clean | clean | ECC errors |
| 4-8 | clean | clean | zeros, "0 errors" |
| 9, 11, 14, 16 | 1 bit flipped | zeros (9-10) | ECC errors (9-10) |

- At 100 MHz no delay works: 0-3 the 1-line status reads are right (the driver waits, reports ECC errors) but the
  4-line data is wrong; 4-8 the status reads 0 ("ready, no error") and the data zeros. The 1-line and 4-line
  phases want different sampling points and their windows no longer overlap.
- **Pad drive makes no difference** (2026-10-21, `pad_ds` on IO14-19, msc checked unchanged): at 100 MHz, ds 7 (the
  board's setting), 10 and 15 all give delays 0-3 and 9-10 ~1100 ECC errors per 64 blocks, 4-8 wrong data with 0
  errors; no correct read at any drive. So edges/ringing are not the limit; the round trip is (W25N02KV ~6-7 ns clock
  to data, plus pads and traces, against a 10 ns bit). Left to try: the other sampling edge (RX_SAMPLE_DLY bit 16,
  half steps) and separate delays for 1-line status and 4-line data reads (driver change). Gain at stake: raw read
  20.6 -> ~25 MB/s, part of the 0.59 s of mounts. Drive stays 7.
- **Bus errors are silent**: the NAND's ECC works inside the chip, so a bit mis-sampled on the bus arrives with no
  error. Hence the delay in the middle of the window, and checksums (never the driver status) for every clock change.
- Chosen: **80 MHz, delay 5** (window 3-8 at both 50 and 80 MHz): kernel `CONFIG_SPI_NAND_SPI_FREQ=80000000`,
  `CONFIG_RT_SPI0_RXSD=5`; U-Boot/SPL dts `spi-max-frequency = <80000000>`, `rx-sample-delay-ns = <6>` (6 ns / 1.25 =
  5). 18 cold boots: every boot read (11) app init 0.876-0.886 s, /bin checksum equal to the 50 MHz read; the others
  booted too (the CH340 attached ~10 s after power-on in the VM, after the script's read; since test039 the console
  is /dev/ttyACM1, up across cold boots).

### 100 MHz with per-phase sample delays, 2026-10-24 (driver patch, sweep100.py)

The SPI driver now writes the RX sample delay per transfer: one pair for 1-line transfers (commands, status reads),
one for 2/4-line data reads (install.sh "per-phase RX sample delay", `dw_spi_set_rx_sample()`, bench
`nand_rxsd d e dm em`). Sweep at 100 MHz, 64 /bin blocks per point against the 50 MHz checksum: 1-line delay 0-10 x
edge 0/1 x multi-line delay 0-10 x edge 0/1 = **484 points, 0 clean** (sweep100.csv). 1-line delays 0-3: the driver
reports read errors at every multi setting; 4-7: "0 errors" (the mis-sampled status, as before) and wrong data at
every multi setting; 8-10: errors again. So the 4-line data has no sampling point at 100 MHz at any delay or edge:
not a conflict between the phases. Failing reads ran at 25.9 MB/s (the gain at stake, 20.6 at 80 MHz).
Not tried yet: the W25N02KV's own output driver strength (its configuration register; the K230 pad drive made no
difference, test017 2026-10-21) - the NAND drives the read data, a weak default would explain slow data edges.
80 MHz, delay 5 stays.

### 100 MHz data with the 1-line phase at 80 MHz, 2026-10-24 (split100.py, verify100.py, boots100.py)

The nibble check (shift100.py) showed the 4-line data correct at 100 MHz with
multi-line delay 4-6: the failures came from the 1-line status reads, which have no window at 100 MHz. So the driver
patch now also keeps a clock divider per phase (`dw_spi_set_multi_hz()`, bench `nand_clk2`): 1-line at the configured
80 MHz, 4-line data at 100 MHz. Sweep of the data delay, 3 reads of 64 /bin blocks each against the 50 MHz checksum:

| Data at 100 MHz | Clean delays | Failing |
|---|---|---|
| Edge 0 | 5-8 | 2-4, 9 |
| Edge 1 | 4-8 | 2-3, 9 |

Edge 1 samples half a step (0.625 ns) later, so the window is 9 contiguous half steps (~5.6 ns); centre = delay 6
edge 1, set as SPI0's boot default in the patch. At the boot defaults (verify100.py):
all of /bin (160 blocks) read 10/10 with the reference checksum, 0 errors, 23.0 MB/s (20.6 at 80 MHz: +12 %);
program + read-back of 32 spare blocks 10/10 without errors (quad programs run at the data clock too), program
5.1 MB/s, read back 20.9 MB/s (2 KiB pages); K230 heap free 7.03 -> 7.31 MB (no fall), chip 42.8 -> 43.7 C.
10 cold boots (boots100.py): all reached app init at 0.311-0.312 s with /bin
and /sdcard mounted (mounts done 0.302-0.303 s) and read all of /bin with the reference checksum, 23.0 MB/s.
Adopted. One of verify100's 10 reads ran at 12.3 MB/s with correct data (open; not seen in the 10 boot reads).

## Metrics (2026-10-21, bench image)

Setup (2026-10-21): USB-C 1 the only power path (USB-C 2 VBUS cut, PR1 VBUS cut), meter (10 A range, 0.1 mA
shown) in its VBUS; cell charged (D1 off). VBAT not measured: P uses 3.8 V nominal (`energy.py --vbat 3.8`).
Board floor: rails off, BL616 awake on USB 60.5 mA; rails on, K230 in reset 86.3 mA; K230 at msh idle (agent running,
READY not answered by the test firmware) 204 mA. A mA-range meter (2-5 ohm) reset the board; an earlier meter had a
burned mA fuse: readings from before the swap are void.

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init | 1.00 s | test022 | UFFS mounts 0.70 s: NAND reads, the clock matters |
| This part's boot share | the mounts, 0.70 s (70 %) | - | at 50 MHz |
| Test run time | read loop 12 s, write loop 21 s, clock steps 10 s, stability 30 s | - | |
| Raw read 50 / 80 MHz | 15.6 / 20.6 MB/s | 25 / 40 MB/s bus | 62 % / 52 % |
| Raw read, data at 100 MHz (2026-10-24, adopted) | 23.0 MB/s | 50 MB/s bus | 46 %; 1-line phases at 80 MHz; one of 10 reads 12.3 MB/s (open) |
| Program | 4.6-5.0 MB/s | chip limit | |
| Current idle / read 50 / read 80 / program | 204 / 225 / 232 / 235-236 mA | floor 204 | USB-C 1 VBUS |
| Voltage under load (min) | **MISSING**: 2026-10-24 the cell was charging (530 mA at idle, user), which hides the board's draw; retake with the cell full (D1 off) | cutoff 3.10 V (bug-004) | at the probes above |
| Energy per MiB read, whole board | 57 mJ at 50 MHz, 45 mJ at 80 MHz | - | 21 % less: shorter time at the 204 mA floor |
| Per day | inside the boot (mounts) and sessions | ENERGY.txt 3 | |
| Temperature die / board (start / peak / end) | die 42.8 / 43.7 / 43.1 C, board (AHT20) 33.4 / 33.9 / 33.9 C (2026-10-24, 10 /bin reads + writes) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | K230 heap free 7.03 / 7.03 / 7.31 MB of 8 MiB, max used 1.53 MB; BL616 free 230960 / min 226748 / 230960 B, 14 blocks, 0 deleted tasks, unchanged over 10 reads: no leak (2026-10-24) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 77.8 MB (/sdcard 176 MiB, /bin 20 MiB layout), kernel ~3.7 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | n/a: the NAND driver is the boot device, part of test008's floor; the per-phase patch adds two fields and two setters | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |
| Power, energy per operation | **MISSING**: as voltage, cell charging on 2026-10-24; at 100 MHz the read takes 10 % less time than at 80 MHz | - |  |

## Boot time

The NAND clock's effect on boot: SPL kernel load and UFFS mount time at each clock (test022 milestones).

## Battery energy

Battery current during raw reads, programs, erases at each NAND clock; energy per MB read and per MB written, and how it changes with the clock (faster clock, shorter on-time).
- **Sweep results landed on the wrong lines** -> a failing read at 100 MHz takes ~3 s, the fixed 1.2 s read window
  queued the next commands -> wait for each command's result line (a sweep script, not shell `eval` with `|` in
  arguments: the shell ran the tokens as commands and nothing reached the board).

## Obstacles

- **100 MHz returns zeros without an error** -> mis-sampled status reads say "ready, no error" -> every clock step
  needs a checksum against a known read, never the driver's status; sample-delay sweep above.
- **`install.sh` run with `sh`** stopped at `set -o pipefail`, and without its SDK argument it only printed usage: the
  next build used the old settings -> `bash install.sh ~/k230_rtos_sdk`, then `leakcam_docker_build.sh
  k230d_rtos_leakcam_defconfig` and `log`; check `rtconfig.h` and the dtb before flashing.
- **Sweep progress invisible for minutes** (2026-10-24) -> `python3 -u ... | tr -d '\r'` buffers in `tr` -> read the
  CSV the sweep writes per point; runners print to a file, not through `tr`.
- **BL616 scanning for "Vlad" by itself after the bridge sessions** (2026-10-24, seen at the sweep's end, rails off) ->
  autoconnect left enabled by the bridge's join -> open: the bridge must disable autoconnect when the K230 goes off
  (radio current while it should be idle).
- **The first nibble-shift check compared erased pages** (2026-10-24, shift100.py) -> pages 0/0, 0/1, 3/5 of /bin are
  all ff: any shift matches -> the script now picks pages with varied bytes at 50 MHz first.
- **I stopped that run by a name pattern and killed my own shell** (exit 144) -> the pattern matched the command
  line of the shell that ran it, the memory rule says kill by recorded PID only -> runners record their PID
  (`echo $! > file`) and are stopped by that.- **K230 3V3 stopped coming up** (2026-10-24 ~20:03, after shift100.py ran fine at ~19:55) -> two flashes failed "no
  K230 USB boot device", and the chained split100.py then ran on the old image (every read None) -> `pt_on`: "FAIL: 3V3
  not up 100 ms after K230_PWR (0 of 5 lines high)", also after a BL616 reboot (sense pins re-inited as inputs, so the
  reading is real); USB-C 1 on the Mac, 540 mA charging, no 29f1 device in VMware -> the RPi camera on the
  connector held the rail down: unplugged, 3V3 up in 1.7 ms again (cause on the camera side open: check the camera
  and its flex before plugging it back); it was plugged in around shift100.py (user): sweep100 and the 2026-10-21 runs
  ran without it, shift100's result was confirmed without it by split100/verify100, so no result is void; runners now chain the test only after a successful flash (`&&`).
- **`leakcam_docker_build.sh log` stopped at once: "empty string invalid as file name"** (2026-10-24) -> my pipefail
  fix quoted the extra make arguments with `printf '%q '`, which prints `''` when there are none -> only real
  arguments are quoted now (comment at the fix, docker/leakcam_docker_build.sh).
