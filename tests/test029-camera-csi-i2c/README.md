# test029: Camera interfaces: CSI frame rate, I2C clock

Status: **planned** (2026-10-21).

After the cameras arrive (OV5647 on CSI0 / CSI2, I2C0 / I2C4).

## Steps

1. I2C: SCL frequency (scope or the sensor's register read time), 100 vs 400 kHz, error-free register dumps.
2. CSI: both sensors at 1280x960 binned, 45 fps (DESIGN.md): frames per second delivered, dropped frames, both at
   once; lanes and data rate.
3. Capture to encoder (test019 with real frames): the pipeline's frame rate.

## Pass

Both cameras at the design rate with no dropped frames for 10 min; I2C clean at the configured clock.
