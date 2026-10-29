# test029: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 150 s): pass

8 boots: both camera I2C buses scanned with no device and no hang; pad_read:

    pad_read

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)

### Results 2026-10-21, image with NAND 80 MHz, /bin 8 MiB (bench commands)

| Check | Result |
|---|---|
| `i2cdetect i2c0` (J4, CAM2) | no device, returns (no hang) |
| `i2cdetect i2c4` (J5, CAM1) | no device, returns (no hang) |
| `pad_read 7 8` (IIC4 SCL/SDA, J5) | 1, 1 (pull-ups and 3V3 present) |
| `pad_read 48 49` (IIC0 SCL/SDA, J4) | 1, 1 |
| `pad_read 62 63` (CAM GPIO, pin 18) | 0, 0 with input enabled |
| GPIO62/63 configuration at run time | ie 1, pu 1, **pd 1** (both pulls) vs the pin table's ie 0, pu 0, pd 1: something re-configures them after the pin init - to find (both pulls waste a little current) |

### Earlier status

Status: **pass** (2026-10-21): both camera I2C buses scan clean and idle high, CAM GPIOs low; re-check with cameras (test020).

### Earlier definition text

Step 3 as written on 2026-10-21:

3. Pin levels in software instead of a meter (user, 2026-10-21: probing the 0.5 mm FPC side is too hard): `pad_read`
   reads each pad's input bit (`di`, bit 31 of the IOMUX register): SCL/SDA of IIC0 and IIC4 = 1 (also proves 3V3 and
   the 4.7 k pull-ups), GPIO62/63 = 0. The meter sheets (j4_bottom.png, j5_top.png, j5_bottom.png) stay for a
   fault hunt.

Boot time (planned): I2C bus init and sensor probe time without cameras (part of today's boot).

Battery energy (planned): Current with the cameras powered but idle (their standby draw while the K230 runs).

## Obstacles

- **Meter probing at J4/J5 too hard** (0.5 mm FPC pins, J4 and J5 on opposite sides) -> pin levels read in software
  (`pad_read`, pad input bits) instead.
- **Pin table comments had R14/R15 and R17/R18 swapped** (IIC4 = J5 = R17/R18, IIC0 = J4 = R14/R15 in the schematic)
  -> pins.py comments corrected; function unaffected.
