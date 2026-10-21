# test020: BL616: wake latency, flash storage, sensor reads

Status: **planned** (2026-10-21).

The always-on side's speed: how fast a wake turns into a K230 power-on, and the cost of its own jobs.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Steps

1. Wake to K230_PWR high: from an RTC wake, a leak (ACOMP) edge and a USB plug (product firmware; a GPIO toggled at
   each step, scope or the K230's rdtime).
2. AHT20 measurement: trigger to data (datasheet 80 ms; the firmware reads at 50 ms then every 5 ms).
3. Easyflash/littlefs: write and read of a key (Wi-Fi credentials, BLE bonds) on the BL616's flash.
4. Leak probe ADC conversion time.

## Pass

Figures recorded; wake to K230 power-on within the design's budget (DESIGN.md).

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - 3V3_SLEEP, VBAT C15.1 (BL616 only).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| BL616 step times (wake -> K230 power-on, I2C read) | not measured yet | I2C 400 kHz (now 100) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

The BL616 side of every wake: wake -> decision -> K230_PWR high (adds to every session before the K230 boot starts).

## Battery energy

The BL616's current while awake (humidity slice, wake handling) and how long; energy per humidity slice (144 per day) and per wake decision.

## Obstacles

None yet.
