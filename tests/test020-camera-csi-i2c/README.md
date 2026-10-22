# test020: Camera interfaces: CSI frame rate, I2C clock

Status: **planned** (2026-10-21).

After the cameras arrive (OV5647 on CSI0 / CSI2, I2C0 / I2C4).

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

I2C0/I2C4, CSI0/CSI2, VICAP, OV5647 sensor driver (CONFIG_MPP_ENABLE_SENSOR_OV5647), ISP files in /bin.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. I2C: SCL frequency (scope or the sensor's register read time), 100 vs 400 kHz, error-free register dumps.
2. CSI: both sensors at 1280x960 binned, 45 fps (DESIGN.md): frames per second delivered, dropped frames, both at
   once; lanes and data rate.
3. Capture to encoder (test027 with real frames): the pipeline's frame rate.

## Pass

Both cameras at the design rate with no dropped frames for 10 min; I2C clean at the configured clock.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| CSI lane rate, I2C clock | not measured yet | OV5647 2-lane, I2C 400 kHz | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |

## Boot time

Sensor probe and init time at boot, both sensors (without cameras the probes fail: their timeout is in today's boot).

## Battery energy

Battery current with one and two sensors streaming (vs K230 idle); energy per second of capture and per still.

## Obstacles

None yet.
