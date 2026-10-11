# CR-10 firmware: Marlin 2.1.2.8 with inductive-probe bilinear leveling

Creality CR-10 (300 x 300 x 400) with an inductive probe on the Z-min input.

## The board

- Creality Melzi, ATmega1284P (signature 1E 97 05).
- Optiboot bootloader: it answers at 115200 baud and occupies the top 1 KB (0x1FC00).
  That leaves 130048 bytes for the firmware.
- `backup_marlin_1.1.9_2020-05-29.hex` is the full 128 KB flash read before the upgrade (2026-10-11):
  Marlin 1.1.9, 3-point leveling, EEPROM disabled.

## Configuration

Built from the official example `config/examples/Creality/CR-10/CrealityV1`
(MarlinFirmware/Configurations, branch release-2.1.2.8). `Configuration_adv.h` is unchanged.
`Configuration.h` changes:

| Setting | Value | Why |
|---|---|---|
| FIX_MOUNTED_PROBE | on | inductive probe, no servo |
| NOZZLE_TO_PROBE_OFFSET | { -47, -3, -1.65 } | measured from the old firmware: after G28 the nozzle stood at X197 Y153 with the probe over 150,150 (G30 agreed); Z offset from its M851 |
| Z_MIN_ENDSTOP_INVERTING, Z_MIN_PROBE_ENDSTOP_INVERTING | true | the probe reads open far from the bed (M119 checked before the first homing) |
| AUTO_BED_LEVELING_BILINEAR | on, 5 x 5 | follows a warped bed (the old 3-point only tilted a plane) |
| Z_SAFE_HOMING | on | Z homes with the probe at the bed centre |
| SLIM_LCD_MENUS | on | needed to fit in 130048 bytes (129240 used) |
| EEPROM_SETTINGS | on (from the example) | M500 saves offsets and the mesh |

Stock values kept: steps/mm 80 / 80 / 400 / 95 (X, Y, Z confirmed from the old firmware), 115200 baud,
stock LCD with SD card.

## Build

```
pip install platformio
# Marlin 2.1.2.8 source; copy the four files here into Marlin/
pio run -e melzi_optiboot_optimized
```

## Flash

The printer must be idle; avrdude resets it through the bootloader:

```
avrdude -p m1284p -c arduino -P COM3 -b 115200 -D -U flash:w:firmware.hex:i
```

After the first flash: `M502` then `M500` (initialise the EEPROM), and `M119` with the probe far from the bed
must show `z_min: open` before any homing.

Restore the old firmware: the same command with the backup file.

## Printing

Start G-code: `G28` then `G29` (probe the 5 x 5 mesh) before every print, or `G28` + `M420 S1` to reuse the
mesh saved with `M500`.
