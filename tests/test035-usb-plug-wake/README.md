# test035: USB plug wake (PGOOD on ACOMP0)

Status: **planned** (2026-10-21).

A USB plug wakes the hibernating BL616 through PGOOD on ACOMP0 (threshold 1.65 V today; 0.5 V with the rev 2 battery
divider, bug-004) and switches it to USB mode (BLE pairing, awake). Unplug returns it to the battery path.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

BL616 ACOMP0 (PGOOD), HBN wake on ACOMP, USB mode switch.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. HBN (pt_sleep / product), plug USB-C 1: wake within ms, PGOOD low, USB mode; unplug: back to battery mode.
2. USB-C 2 only (through D2): the same?
3. With the K230 running (bug-002 back-feed): what the BL616 sees, and when power arrives after the session.
4. Plug/unplug 100 times: no missed wake, no stuck state.

## Pass

Every plug wakes and every unplug returns to battery mode; latency recorded.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VUSB C2.1, PGOOD (BL616 IO03), VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| wake latency on plug | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |

## Boot time

Plug to the BL616 in USB mode (console up).

## Battery energy

n/a while on USB (the charger powers the board); the unplug path's return to sleep is checked by test031's sleep current afterwards.

## Obstacles

None yet.
