# test028: Camera buses and IO without cameras

Status: **planned** (2026-10-21).

Before the cameras arrive: the two camera I2C buses and the CAM_IO1 lines must be idle and not stuck.

## Steps

1. K230 running: IIC0 (CAM2, J4, CSI0) and IIC4 (CAM1, J5, CSI2) SCL/SDA high (R17/R18, R14/R15 4.7 k to 3V3); an
   I2C scan of both returns no device and does not hang.
2. GPIO62/63 (J4/J5 pin 18, CAM_IO1) driven low by the pin init.
3. Camera supply pins on J4/J5 (meter) as the schematic says, with nothing plugged in.

## Pass

Buses idle high, scans end cleanly, CAM_IO1 low, connector supplies as designed.


## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
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
