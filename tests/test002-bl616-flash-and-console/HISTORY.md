# test002: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 51 s): pass

8 rounds of `pt_status`: leak node dry, AHT20 read, USB detected; all good.


### Automated run 2026-10-25 (run.py, 50 s): pass

8 rounds of `pt_status`: leak node dry, AHT20 read, USB detected; all good.

### Run 2026-10-17 (Results)

| Step | Result | |
|---|---|---|
| Flash `bl616_powertest` over PR1 (`make flash`) | flashed, console on `/dev/ttyACM0` | pass |
| `pt_status` | leak node 1649 mV dry; AHT20 53.9 %RH, 25.8 C | pass |
| `pt_usb` | USB-C 1 plug/unplug reported (BQ24072 PGOOD) | pass |
| `pt_leak` (dry node) | ~1650 mV | pass |

**Pass.** Wet leak test and Wi-Fi: with the product BL616 firmware (later).

### Superseded metric values

- Test run time, note (2026-10-21): "setup: 2 "connect to Linux" clicks + 1 EN tap (none since 2026-10-22: `--boot`)".

## Obstacles

- **`isp` still missed the ROM 5 times of ~20** (2026-10-23/24) -> VMware prompts missed or held; popups off since
  2026-10-24, devices attach by themselves; tools wait 20 s, a miss is now a fault to debug.
- **`reboot uart` with the watchdog running looped the ROM until a 3V3 cut** (2026-10-23) -> the 8 s watchdog survived
  the reset into the ROM and reset it before USB came up; the kept user-boot field sent every reset back -> `isp`
  (firmware/bl616/wdg_guard.c: watchdog off, USB detach 0.5 s, then the ROM); every BL616 firmware has it,
  `bl616_isp.py` sends it. `bl616_isp.py` also took the firmware's second port (the K230 console) for the shell ->
  interface 0 only.
- **`--boot` refused once ("b''", 2026-10-22 22:29)** -> the ROM had closed the ISP session: it ends one after ~2 s
  without a command, and the flasher's container took longer than that to exit -> `--boot` hand-shakes again
  (0x55 bytes until "OK") and retries; done by hand once, then built into bl616_isp.py.
- **The EN tap after every flash** (2026-10-22) -> `reboot uart` sets the user-boot field in HBN_RSV2
  (0x2000F108), which survives a reset, so every reset lands in the ROM again; only EN clears the HBN domain (the
  2026-10-21 note above was wrong) -> `bl616_isp.py --boot`: in the ROM session the flasher leaves open, ISP memory
  write 0x50 HBN_RSV2 = 0 (as the vendor's `clear_boot_status()` in bflb-mcu-tool) and ISP reset 0x21; `make flash`
  runs it. Verified 2026-10-22 09:38: `reboot uart` -> new firmware console in 18 s, no hand action but the 2 VMware
  clicks.
- **Flashing needed BOOT link + EN touch every time** -> the ROM also starts download mode after `reboot uart`
  (SDK bl616dk shell: HBN user-boot 1 + power-on reset) -> `make flash` does it over the running console
  ([bl616_isp.py](../../tools/power-bringup/bl616_isp.py)). **Verified 2026-10-21:** `reboot uart` -> ROM `Bouffalo
  CDC` (349b:6160) on USB (after "connect to Linux"), flash 3.8 s at 2 Mbaud, SHA verified. The ROM stays in download
  mode afterwards: `BLFlashCommand --reset` logs "Reset cpu" but the chip does not restart -> one EN tap starts the
  new image (banner build time changed, `pt_status` normal); the HBN user-boot flag does not bring the ROM back.
  Flashing: no BOOT link, one EN tap (was link + two taps).
- **USB-C cable not detected** -> a USB-C port needs 5.1 k CC-GND -> resistor on the yellow CC wire (6.7 k works).
- **Flash mode ignored** -> BOOT sampled only at reset -> link 4-6 first, then tap EN. Later: `reboot uart`.
- **New firmware not running after flash** -> the ROM stays in flash mode -> one EN tap.
- **`BLFlashCommand-arm` won't start** -> 32-bit armhf binary on arm64 -> `make flash` runs the x86 tool in the container.
