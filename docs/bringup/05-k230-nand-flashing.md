# 5. K230 NAND flashing

- **x86-only `k230_flash_cli`** -> run in the container with `--privileged -v /dev/bus/usb`.
- **"No devices found. Polling" after the loader** -> no USB hot-plug events in the container ->
  `--network host -v /run/udev:/run/udev:ro` (+ "connect to Linux").
- **Read-back fails** -> loader can't read SPI NAND -> verify by booting.
- **Re-flash needs B1 short** -> `pt_boot1 high` + `pt_hold` + `pt_run`; flash without `--auto-reboot`.
- **Every flash:** pad check `64 pads checked, OK` first.
