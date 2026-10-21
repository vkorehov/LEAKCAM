# test024: DDR stress (64 MB at 2667 MT/s)

Status: **planned** (2026-10-21).

SPL trains the DDR at 2667 MT/s on every boot (test006). A marginal setting shows only as rare random crashes.
This test runs a pattern memory test over most of the free RAM for a long time.

## Steps

1. A temporary memtest command (SDK copy only, as the test014 bench): allocate the largest free block, write and
   verify walking ones/zeros, address-in-address and random patterns, report errors with address and pattern.
2. Run it in a loop for >= 1 h, the console recorded (skill k230-rtsmart-hangs, `console_record.py`).
3. Repeat with the KPU and the encoder busy (heat, power noise) if the first run is clean.
4. Repeat after a few cold starts (each start retrains).

## Pass

0 errors over >= 1 h and >= 5 cold starts; no reset (a silent reset = watchdog: the test must sleep between passes).


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| memtest MB/s under load, errors | not measured yet | DDR top (test015) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

Each DDR setting's training time (with test015), cold starts recorded.

## Battery energy

Battery current at 2667 and 3200 MT/s under the same load: the cost of the faster DDR per second, against the time it saves (test015, test021).
