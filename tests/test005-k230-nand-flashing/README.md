# test005: K230 NAND flashing, 2026-10-17/18

Every flash: [firmware/k230_board/flash_nand.sh](../../firmware/k230_board/flash_nand.sh) (pad check, erase
0..248M, write the kdimg), K230 in USB boot (`pt_boot1 high`, `pt_hold`, `pt_run`).

| Step | Result | |
|---|---|---|
| Full image to NAND (pad check OK) | written; boot: see test006 | pass |
| Re-flash a booting board without the B1 short | `pt_boot1 high` + reset: ROM falls back to USB boot | pass |
| Re-flash over an old image | stale blocks broke UFFS until the range is erased first | fixed (`flash_nand.sh`) |

**Pass.**

## Obstacles

- **x86-only `k230_flash_cli`** -> run in the container with `--privileged -v /dev/bus/usb`.
- **"No devices found. Polling" after the loader** -> no USB hot-plug events in the container ->
  `--network host -v /run/udev:/run/udev:ro` (+ "connect to Linux" on the Mac).
- **Read-back fails** -> the loader can't read SPI NAND -> verify by booting.
- **Re-flash needs B1 short** -> `pt_boot1 high` + `pt_hold` + `pt_run`; flash without `--auto-reboot`.
- **Re-flash leaves stale blocks** -> the loader does not erase beyond each image -> erase first.
