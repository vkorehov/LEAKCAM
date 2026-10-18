# 5. K230 NAND flashing

- **x86-only `k230_flash_cli`** -> run in the container with `--privileged -v /dev/bus/usb`.
- **"No devices found. Polling" after the loader** -> no USB hot-plug events in the container ->
  `--network host -v /run/udev:/run/udev:ro` (+ "connect to Linux").
- **Read-back fails** -> loader can't read SPI NAND -> verify by booting.
- **Re-flash needs B1 short** -> `pt_boot1 high` + `pt_hold` + `pt_run`; flash without `--auto-reboot`.
- **Re-flash leaves stale blocks** -> the loader does not erase beyond each image -> erase first.
- **Every flash:** `firmware/k230_board/flash_nand.sh` (pad check, erase 0..248M, write).
