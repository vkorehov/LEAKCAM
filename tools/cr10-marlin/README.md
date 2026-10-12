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
| NOZZLE_TO_PROBE_OFFSET | { -47, -3, -1.85 } | X/Y measured from the old firmware: after G28 the nozzle stood at X197 Y153 with the probe over 150,150 (G30 agreed); Z by a paper test on the 80 °C bed (the old -1.65 left the first layer in the air) |
| Z_MIN_ENDSTOP_INVERTING, Z_MIN_PROBE_ENDSTOP_INVERTING | true | the probe reads open far from the bed (M119 checked before the first homing) |
| AUTO_BED_LEVELING_BILINEAR | on, 5 x 5 | follows a warped bed (the old 3-point only tilted a plane) |
| Z_SAFE_HOMING | on | Z homes with the probe at the bed centre |
| X_BED_SIZE, PROBING_MARGIN (LEFT, BACK) | 300; 20 (22, 10) | the plate, measured with the nozzle, reaches 7.8 mm left of X0 and 10 mm past Y300; the mesh keeps 2 cm from its edges and is centred on it (the probe reaches X 273 at most) |
| TEMP_SENSOR_0; HOTEND0_BETA, HOTEND0_SH_C_COEFF | 1000 (custom); 4630, 9.5729e-08 | Marlin's table 1 (EPCOS 100k) ends at 320 °C, too short for a 320 °C cutoff. The custom thermistor computes the same curve: beta and Steinhart-Hart C fitted to table 1, within 1.3 °C from 25 to 320 °C, so PID and calibration stay valid |
| HEATER_0_MAXTEMP, HOTEND_OVERSHOOT; BED_MAXTEMP | 320, 20; 130 | nozzle settable up to 300 °C (only with an all-metal heat break; a PTFE liner stays at 240), bed up to 120 °C (insulated) |
| SHOW_BOOTSCREEN | off | frees flash for the custom thermistor math |
| DEFAULT_KP/KI/KD; DEFAULT_BED_KP/KI/KD | 31.41 / 2.72 / 90.70; 192.76 / 35.12 / 705.31 | PID autotune: `M303 E0 S250 C8 U1` (the hot end overshot 5 °C with the stock values) and `M303 E-1 S80 C4 U1`. The insulated bed needs about 10 min per cycle, and C8 runs into Marlin's autotune timeout |
| SLIM_LCD_MENUS | on | needed to fit in 130048 bytes |
| EEPROM_SETTINGS | on (from the example) | M500 saves offsets and the mesh |

Steps/mm 80 / 80 / 400 / **140**. X, Y, Z are confirmed from the old firmware. E is for the dual-gear extruder: the old firmware's
backup has 142, not the example's 95, which gave a third too little plastic. Measured with the Bowden tube unhooked:
200 mm commanded gave 204.2 mm, so 142 x 200 / 204.2 = 139.08, set to 140. Also kept: 115200 baud, the stock LCD with the SD card.

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
must show `z_min: open` before any homing. The EEPROM keeps the probe offset and the mesh across reflashes of the
same version (check with `M501`, `M851`).

Z offset by a paper test: heat the bed (80 °C) and the nozzle (150 °C), then:

1. `G28`, `M420 S1`, nozzle to X150 Y150, `M211 S0`.
2. Lower in 0.05-0.1 mm steps until a sheet of paper still slides in but the nozzle holds it a little.
3. `M851 Z<old offset + that step>`, `M211 S1`, `M500`.

On 2026-10-11 this went -1.65 -> -1.95; after the nozzle and heat-break swap -1.75; on 2026-10-12, after a fresh G29, -1.85.

`firmware.hex` here is the current build. Restore the old firmware: the same command with the backup file.

## Printing

Start G-code: `G28` then `G29` (probe the 5 x 5 mesh) before every print, or `G28` + `M420 S1` to reuse the
mesh saved with `M500`.
