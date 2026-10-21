# test025: K230 die temperature (TS)

Status: **planned** (2026-10-21).

`CONFIG_RT_USING_TS=y`: the K230's die temperature sensor. How hot the K230 gets in a session, and with the
enclosure closed.

## Steps

1. Read the TS device at idle right after boot, then every 10 s for 10 min.
2. Same under load: KPU change net in a loop, then the encoder (test026), both cameras when they arrive.
3. Same with the board in the closed enclosure.

## Pass

Readings plausible (idle a few C above room temperature, monotonic warm-up), and under worst-case load well below
the K230's limit (datasheet); note the margin for the enclosure.


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| SoC temperature vs time and load | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

n/a (the TS read is in the session); boot temperature recorded at READY.

## Battery energy

The current at each load step logged next to the temperature (self-heating follows power).
