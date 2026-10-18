# test002: BL616 flash and console (PR1), 2026-10-17

PR1 cable and flash mode: [tools/power-bringup](../../tools/power-bringup/README.md) sections 2-3; test
firmware [bl616_powertest](../../tools/power-bringup/bl616_powertest).

| Step | Result | |
|---|---|---|
| Flash `bl616_powertest` over PR1 (`make flash`) | flashed, console on `/dev/ttyACM0` | pass |
| `pt_status` | leak node 1649 mV dry; AHT20 53.9 %RH, 25.8 C | pass |
| `pt_usb` | USB-C 1 plug/unplug reported (BQ24072 PGOOD) | pass |
| `pt_leak` (dry node) | ~1650 mV | pass |

**Pass.** Wet leak test and Wi-Fi: with the product BL616 firmware (later).

## Obstacles

- **USB-C cable not detected** -> a USB-C port needs 5.1 k CC-GND -> resistor on the yellow CC wire (6.7 k works).
- **Flash mode ignored** -> BOOT sampled only at reset -> link 4-6 first, then tap EN. Later: `reboot uart`.
- **New firmware not running after flash** -> the ROM stays in flash mode -> one EN tap.
- **`BLFlashCommand-arm` won't start** -> 32-bit armhf binary on arm64 -> `make flash` runs the x86 tool in the container.
