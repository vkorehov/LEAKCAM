# test028: Camera buses and IO without cameras

Status: **partial pass** (2026-10-21): both buses scan clean, no hang; pin levels by `pad_read` (next bench image).

Before the cameras arrive: the two camera I2C buses and the CAM_IO1 lines must be idle and not stuck.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Steps

1. K230 running: IIC0 (CAM2, J4, CSI0) and IIC4 (CAM1, J5, CSI2) SCL/SDA high (R17/R18, R14/R15 4.7 k to 3V3); an
   I2C scan of both returns no device and does not hang.
2. GPIO62/63 (J4/J5 pin 18, CAM_IO1) driven low by the pin init.
3. Pin levels in software instead of a meter (user, 2026-10-21: probing the 0.5 mm FPC side is too hard): `pad_read`
   reads each pad's input bit (`di`, bit 31 of the IOMUX register): SCL/SDA of IIC0 and IIC4 = 1 (also proves 3V3 and
   the 4.7 k pull-ups), GPIO62/63 = 0. The meter sheets (j4_bottom.png, j5_top.png, j5_bottom.png) stay for a
   fault hunt.

## Pass

Buses idle high, scans end cleanly, CAM_IO1 low, connector supplies as designed.

## Results

### 2026-10-21, image with NAND 80 MHz, /bin 8 MiB (bench commands)

| Check | Result |
|---|---|
| `i2cdetect i2c0` (J4, CAM2) | no device, returns (no hang) |
| `i2cdetect i2c4` (J5, CAM1) | no device, returns (no hang) |
| SCL/SDA high, GPIO62/63 low | not measured yet: `pad_read` comes with the next bench image |

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | 2 scans, < 1 s each (8 s read window) | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

I2C bus init and sensor probe time without cameras (part of today's boot).

## Battery energy

Current with the cameras powered but idle (their standby draw while the K230 runs).

## Obstacles

- **Meter probing at J4/J5 too hard** (0.5 mm FPC pins, J4 and J5 on opposite sides) -> pin levels read in software
  (`pad_read`, pad input bits) instead.
