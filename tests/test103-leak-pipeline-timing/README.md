# test103: Whole leak pipeline, end to end

Status: **later** (excluded for now, user 2026-10-21): needs the cameras, Wi-Fi and the product BL616 firmware.

Every part measured on its own (KPU 0.5 ms, SDIO, NAND, encoder): the time from a wake to the server's answer,
with everything running together.

## Steps

1. Wake -> K230 READY (test101) -> capture both cameras -> change net -> history -> POST /v1/check -> answer ->
   (leak) RTMP clip start: timestamps at each step on the K230 console (console_record.py).
2. Same with the server answering "leak" (the clip path) and "no leak" (back to sleep).
3. Battery cost: with test102's current, the energy per wake for each path.

## Pass

Recorded timeline; the longest step named; the leak-to-video time within what the user expects.


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| leak -> video steps | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

Boot share of the leak-to-video time (test021).

## Battery energy

Energy per leak event, wake to clip uploaded and power-off.
