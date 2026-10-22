# test005: K230 NAND flashing, 2026-10-17/18

Status: **pass** (2026-10-17).

Every flash: [firmware/k230_board/flash_nand.sh](../../firmware/k230_board/flash_nand.sh) (pad check, erase
0..248M, write the kdimg), K230 in USB boot (`pt_boot1 high`, `pt_hold`, `pt_run`).

## Setup

Not written at the time; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

K230 burn tool (k230_flash_cli), SPI NAND loader; check_pad_voltage.py.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

Not written at the time; the steps are the rows of the Results tables.

## Pass

Not written at the time; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| Full image to NAND (pad check OK) | written; boot: see test006 | pass |
| Re-flash a booting board without the B1 short | `pt_boot1 high` + reset: ROM falls back to USB boot | pass |
| Re-flash over an old image | stale blocks broke UFFS until the range is erased first | fixed (`flash_nand.sh`) |

**Pass.**

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (to fill on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | |
| Test run time | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | not measured: run before the metrics rule (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | not measured: run before the metrics rule (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | not measured: run before the metrics rule (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |

## Boot time

n/a: flashing.

## Battery energy

n/a: flashing runs from USB power.

## Obstacles

- **x86-only `k230_flash_cli`** -> run in the container with `--privileged -v /dev/bus/usb`.
- **"No devices found. Polling" after the loader** -> no USB hot-plug events in the container ->
  `--network host -v /run/udev:/run/udev:ro` (+ "connect to Linux" on the Mac).
- **Read-back fails** -> the loader can't read SPI NAND -> verify by booting.
- **Re-flash needs B1 short** -> `pt_boot1 high` + `pt_hold` + `pt_run`; flash without `--auto-reboot`.
- **Re-flash leaves stale blocks** -> the loader does not erase beyond each image -> erase first.
- **`flash_nand.sh` said "flashed" after a failed write** (2026-10-21: the board lost power during the /sdcard write) ->
  `k230_flash_cli` exits 0 when a write fails -> the script now stops on `^(Write|Erase) .* failed`. The line `usb bulk
  read data failed ... LIBUSB_ERROR_TIMEOUT` during the long erase is normal (the erase still ends "done").
