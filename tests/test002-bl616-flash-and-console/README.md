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



## Boot time

BL616 boot to its first useful step (USB console up, or the battery path's decision) - test020 covers the BL616 wake path.

## Battery energy

n/a: flashing and the console run from USB power.

## Obstacles

- **USB-C cable not detected** -> a USB-C port needs 5.1 k CC-GND -> resistor on the yellow CC wire (6.7 k works).
- **Flash mode ignored** -> BOOT sampled only at reset -> link 4-6 first, then tap EN. Later: `reboot uart`.
- **New firmware not running after flash** -> the ROM stays in flash mode -> one EN tap.
- **`BLFlashCommand-arm` won't start** -> 32-bit armhf binary on arm64 -> `make flash` runs the x86 tool in the container.
- **Flashing needed BOOT link + EN touch every time** -> the ROM also starts download mode after `reboot uart`
  (SDK bl616dk shell: HBN user-boot 1 + power-on reset) -> `make flash` does it over the running console
  ([bl616_isp.py](../../tools/power-bringup/bl616_isp.py)). **Verified 2026-10-21:** `reboot uart` -> ROM `Bouffalo
  CDC` (349b:6160) on USB (after "connect to Linux"), flash 3.8 s at 2 Mbaud, SHA verified. The ROM stays in download
  mode afterwards: `BLFlashCommand --reset` logs "Reset cpu" but the chip does not restart -> one EN tap starts the
  new image (banner build time changed, `pt_status` normal); the HBN user-boot flag does not bring the ROM back.
  Flashing: no BOOT link, one EN tap (was link + two taps).

## Metrics (2026-10-21, ISP flash of bl616_powertest)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY | n/a | - | no K230 |
| This part's boot share | EN tap -> console on USB ~5 s | - | includes the VMware re-attach |
| Test run time | `make flash` 26 s (ISP 1 s, flasher 3.8 s, the rest the container + waits) | - | setup: 2 "connect to Linux" clicks + 1 EN tap |
| Flash write | 116.6 KB in 571 ms = 204 KB/s | UART 2 Mbaud = 200 KB/s | ~100 % (the ROM link is the limit) |
| Current idle / during (min / avg / peak) | not measured: meter not connected | - | on USB (not battery) |
| Power, energy per operation | n/a | - | flashing only on USB power |
| Per day | n/a | - | |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
