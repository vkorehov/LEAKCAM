# test018: K230 USB-C 1: USB boot and flashing speed

Status: **planned** (2026-10-21).

USB-C 1's data lines are used only for USB boot and flashing (the product needs no USB data). Record what the
flash path achieves, and its total time, as a reference for production programming.

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

K230 ROM USB boot + burn tool (host side).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. From the flash logs (flash_nand.sh): per-part write speed and the whole erase + write time.
2. USB speed the ROM/loader enumerate at (high speed or full speed; `lsusb -t`).
3. BL616 flashing over PR1: total time (test firmware 4 s, product 13 s, 2026-10-21) and the baud rate.

## Pass

Figures recorded (K230 writes 3.7-5.5 MB/s seen 2026-10-21); a production flash time per board.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| USB NAND flashing write speed | not measured yet | NAND program limit (test016) | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test038's floor | test038 floor | bytes, boot ms, RAM, current |

## Boot time

n/a: flashing.

## Battery energy

n/a: flashing runs from USB power.

## Obstacles

None yet.
