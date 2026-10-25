# test005: K230 NAND flashing, 2026-10-17/18

Status: **pass** (2026-10-17).

Every flash: [firmware/k230_board/flash_nand.sh](../../firmware/k230_board/flash_nand.sh) (pad check, erase
0..248M, write the kdimg), K230 in USB boot (`pt_boot1 high`, `pt_hold`, `pt_run`).

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

K230 burn tool (k230_flash_cli), SPI NAND loader; check_pad_voltage.py.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| Full image to NAND (pad check OK) | written; boot: see test006 | pass |
| Re-flash a booting board without the B1 short | `pt_boot1 high` + reset: ROM falls back to USB boot | pass |
| Re-flash over an old image | stale blocks broke UFFS until the range is erased first | fixed (`flash_nand.sh`) |

**Pass.**

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Automated run 2026-10-25 (run.py, 93 s): pass

5 NAND boots of the flashed image, /sdcard/app:

    leakcam_wake                            5181000
    leakcam_stream                          1030880
    leakcam_change.kmodel                   211488
    leakcam_hist                            100376
    memtest                                 67120
    leakcam_agent                           87552

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.64 / 0.64 s (median / max of 5; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.63, msh 0.64); longest step to mounted 0.38 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | n/a: flashing is not part of a boot; the mounts take 0.31 s (test022) (2026-10-25) | - | |
| Test run time | 93 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | flash speed: test019 (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | NAND boot to msh idle 0.220 A (test006, 2026-10-25); flashing current not read (2026-10-25, from recorded readings) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.36 V min (K230 ADC ch 1, test023 calibration, 5 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: flashing runs on USB power (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: a factory / update step (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 36.7 / 39.8 / 39.2 C; board 31.8 / 31.8 / 31.7 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; K230 heap available 7025152 B steady; leak: n/a, 5 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | n/a: flashing writes the image itself; its contents are the other tests' features (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (NAND erase/program and USB 2.0 rates): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

n/a: flashing.

## Battery energy

n/a: flashing runs from USB power.

## Obstacles

- **x86-only `k230_flash_cli`** -> run in the container with `--privileged -v /dev/bus/usb`.
- **"No devices found. Polling" after the loader** -> no USB hot-plug events in the container ->
  `--network host -v /run/udev:/run/udev:ro` (+ "connect to Linux" on the Mac until 2026-10-24; popups are off now).
- **Read-back fails** -> the loader can't read SPI NAND -> verify by booting.
- **Re-flash needs B1 short** -> `pt_boot1 high` + `pt_hold` + `pt_run`; flash without `--auto-reboot`.
- **Re-flash leaves stale blocks** -> the loader does not erase beyond each image -> erase first.
- **`flash_nand.sh` said "flashed" after a failed write** (2026-10-21: the board lost power during the /sdcard write) ->
  `k230_flash_cli` exits 0 when a write fails -> the script now stops on `^(Write|Erase) .* failed`. The line `usb bulk
  read data failed ... LIBUSB_ERROR_TIMEOUT` during the long erase is normal (the erase still ends "done").

None in the run of 2026-10-25.
