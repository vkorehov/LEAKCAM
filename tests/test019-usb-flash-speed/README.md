# test019: K230 USB-C 1: USB boot and flashing speed

Status: **pass** (2026-10-22, 2 runs): K230 whole image 25.7 / 26.9 s (erase 8 s, write 3.1-5.6 MB/s, USB 2.0 high speed); BL616 ISP flasher 4 s at 2 Mbaud, no hand action; current during the 15 s write not read.

USB-C 1's data lines are used only for USB boot and flashing (the product needs no USB data). Record what the
flash path achieves, and its total time, as a reference for production programming.

## Setup

K230: `pt_off`, `pt_boot1 high`, `pt_on`, `pt_run` on the BL616 console (29f1:0230 attaches by itself since 2026-10-24;
before, "connect to Linux"), then
`firmware/k230_board/flash_nand.sh` (erase 0-248 MB, then the kdimg; k230_flash_cli in the build container), timed
by a wrapper script; `lsusb -t` / `lsusb -v` while the boot device is up. BL616: `make flash` (bl616_isp.py:
`reboot uart`, BLFlashCommand 2 Mbaud over USB CDC, `--boot`). Meter in USB-C 1 VBUS, 10 A range.

## Needs (build features and dependencies)

K230 ROM USB boot + burn tool (host side).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. From the flash logs (flash_nand.sh): per-part write speed and the whole erase + write time.
2. USB speed the ROM/loader enumerate at (high speed or full speed; `lsusb -t`).
3. BL616 flashing over PR1: total time (test firmware 4 s, product 13 s, 2026-10-21) and the baud rate.

## Pass

Figures recorded (K230 writes 3.7-5.5 MB/s seen 2026-10-21); a production flash time per board.

## Results

### 2026-10-22, run 1 (kdimg 45.4 MB, 4 Mbaud link image)

| Step | Time / speed |
|---|---|
| `pt_run` -> K230 boot device on USB | 6.1 s (incl. the VMware "connect to Linux") |
| USB link | high speed 480 Mbit/s (bcdUSB 2.00, bulk 512 B), ROM and burn loader |
| ROM -> burn loader (re-enumerates as UBOOT) | inside the 25.7 s; loader chunk out 128 KB, in 32 KB |
| Erase 0 .. 248 MB | 8.0 s (31 MB/s) |
| SPL A/B, U-Boot, env, kernel A/B (0.07-1.47 MB each) | 3.1-3.7 MB/s |
| /bin 8.65 MB, /sdcard 34.6 MB | 5.6, 5.3 MB/s |
| **Whole K230 flash** (flash_nand.sh start -> end) | **25.7 s** |
| BL616 test firmware 121 KB (`make flash`: reboot uart -> new console) | 18 s, of which flasher 4 s; no EN tap (`--boot`) |

### 2026-10-22, run 2 (single-rate images)

K230 26.9 s (erase 8.0 s; one `LIBUSB_ERROR_TIMEOUT` during the erase, normal, it ended "done"). BL616 `make flash`
09:59:26 -> console ~10:00:10, of which ~35 s waiting for the ROM's "connect to Linux"; flasher 4 s.

Production per board (both chips, no hand action but plugging): ~45 s plus the K230 boot-device wait.

### Automated run 2026-10-25 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.6 s, ok; 1.85 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt

### Automated run 2026-10-25 (run.py, 56 s): pass

flash_nand.sh (erase 0..248M + write 45441024 B): 24.6 s, ok; 1.85 MB/s overall incl. erase; then booted to msh. Log: flash_last.txt

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.46 / 0.46 s (median / max of 1; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.45, msh 0.46); longest step to mounted 0.20 s (2026-10-25, run.py) | target (test022) | longest step: |
| This part's boot share | n/a: flashing is not part of a boot (2026-10-25) | - | |
| Test run time | 25.7 s K230 + 18 s BL616 | - | |
| USB NAND flashing write speed | 5.3-5.6 MB/s large parts, 3.1-3.7 small | NAND program limit (test017) | whole flash 25.7 s |
| Current idle / during (min / avg / peak) | 207 mA rails on in USB boot; write not measured (15 s, too short to read) | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | VBAT 4.37 V min (K230 ADC ch 1, test023 calibration, 1 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: on USB power, never from the cell (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: flashing is a factory / update step (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 39.8 / 39.8 / 39.8 C; board 31.8 / 31.8 / 31.8 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; K230 heap available 7025152 B steady; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 45441024 B (test image: product programs + test tools) (2026-10-25) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-25) | smallest code (rule) | |
| Features over the minimal image | n/a: flashing writes the image itself (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (NAND erase/program and USB 2.0 rates): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

n/a: flashing.

## Battery energy

n/a: flashing runs from USB power.

## Obstacles

- **The BL616 lost the NAND boot commands after the flash** (09:48, open): `pt_off`, `pt_boot1 low`, `pt_on`,
  `pt_run` sent 30 s after flashing found the rails off and BOOT1 still high at 09:50 (62 mA, "K230 down"). (Its
  banner in the next read is not a restart: the test firmware prints it whenever the console is opened.) Resent with
  each answer read back -> booted; run 2 (each command's answer read) went through first time. Always read the
  answers back.
- **Build line `An error occurred: 'CONFIG_FAST_BOOT_DELETE_ORIGIIN_FILE'`** (the SDK's image script reading an option
  the leakcam defconfig does not set): the build ends 0 and the kdimg is complete; harmless.

None in the run of 2026-10-25.

None in the run of 2026-10-25.
