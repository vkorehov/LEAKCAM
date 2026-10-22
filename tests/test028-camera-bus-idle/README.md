# test028: Camera buses and IO without cameras

Status: **pass** (2026-10-21): both camera I2C buses scan clean and idle high, CAM GPIOs low; re-check with cameras (test019).

Before the cameras arrive: the two camera I2C buses and the CAM_IO1 lines must be idle and not stuck.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

I2C0/I2C4 drivers, i2cdetect (msh), fpioa (pad_read in leakcam_bench.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

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
| `pad_read 7 8` (IIC4 SCL/SDA, J5) | 1, 1 (pull-ups and 3V3 present) |
| `pad_read 48 49` (IIC0 SCL/SDA, J4) | 1, 1 |
| `pad_read 62 63` (CAM GPIO, pin 18) | 0, 0 with input enabled |
| GPIO62/63 configuration at run time | ie 1, pu 1, **pd 1** (both pulls) vs the pin table's ie 0, pu 0, pd 1: something re-configures them after the pin init - to find (both pulls waste a little current) |

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
| Image (sizes) | kdimg 45.4 MB (/sdcard 32 MiB, /bin 8 MiB one ISP set), kernel 3.75 MB, /bin 0.68 MB, /sdcard programs 6.76 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, --gc-sections (no -ffunction-sections, no LTO) | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test038's floor | test038 floor | bytes, boot ms, RAM, current |

## Boot time

I2C bus init and sensor probe time without cameras (part of today's boot).

## Battery energy

Current with the cameras powered but idle (their standby draw while the K230 runs).

## Obstacles

- **Meter probing at J4/J5 too hard** (0.5 mm FPC pins, J4 and J5 on opposite sides) -> pin levels read in software
  (`pad_read`, pad input bits) instead.
- **Pin table comments had R14/R15 and R17/R18 swapped** (IIC4 = J5 = R17/R18, IIC0 = J4 = R14/R15 in the schematic)
  -> pins.py comments corrected; function unaffected.
