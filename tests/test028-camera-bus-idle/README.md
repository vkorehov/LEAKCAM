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
