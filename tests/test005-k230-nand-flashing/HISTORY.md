# test005: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 93 s): pass

5 NAND boots of the flashed image, /sdcard/app:

    leakcam_wake                            5181000
    leakcam_stream                          1030880
    leakcam_change.kmodel                   211488
    leakcam_hist                            100376
    memtest                                 67120
    leakcam_agent                           87552

### Run 2026-10-17/18: pass

Status: **pass** (2026-10-17).

| Step | Result | |
|---|---|---|
| Full image to NAND (pad check OK) | written; boot: see test006 | pass |
| Re-flash a booting board without the B1 short | `pt_boot1 high` + reset: ROM falls back to USB boot | pass |
| Re-flash over an old image | stale blocks broke UFFS until the range is erased first | fixed (`flash_nand.sh`) |

**Pass.**

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Obstacles

- **`flash_nand.sh` said "flashed" after a failed write** (2026-10-21: the board lost power during the /sdcard write) ->
  `k230_flash_cli` exits 0 when a write fails -> the script now stops on `^(Write|Erase) .* failed`. The line `usb bulk
  read data failed ... LIBUSB_ERROR_TIMEOUT` during the long erase is normal (the erase still ends "done").
- **x86-only `k230_flash_cli`** -> run in the container with `--privileged -v /dev/bus/usb`.
- **"No devices found. Polling" after the loader** -> no USB hot-plug events in the container ->
  `--network host -v /run/udev:/run/udev:ro` (+ "connect to Linux" on the Mac until 2026-10-24; popups are off now).
- **Read-back fails** -> the loader can't read SPI NAND -> verify by booting.
- **Re-flash needs B1 short** -> `pt_boot1 high` + `pt_hold` + `pt_run`; flash without `--auto-reboot`.
- **Re-flash leaves stale blocks** -> the loader does not erase beyond each image -> erase first.
