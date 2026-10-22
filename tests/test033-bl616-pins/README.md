# test033: every BL616 pin: function, levels, back-feed

Status: **planned** (2026-10-21).

The K230's pads were checked by configuration (check_pad_voltage.py: every bank's voltage against the board, before
each flash) and by line tests with both ends (test014 SDIO). This is the same for every BL616 (Ai-M62-CBS, U11) pin.

## Pins (board_pins.h, WIFI.SchDoc)

| Pin | Net | Function | Checks |
|---|---|---|---|
| IO00 | K230_RSTN (Q4 gate, R76 100 k up) | output, high = K230 held in reset | reset holds/releases (pt_hold/pt_run), level in HBN (R76) |
| IO01 | K230_ALIVE (R32 100 k to 3V3) | input | heartbeat edges (test024), parked while 3V3 off |
| IO02 | AI_BOOT (R35 100 k down, PR1.4) | BL616 boot strap | low at every reset; flashing link works |
| IO03 | PGOOD (R23 100 k to 3V3_SLEEP) | ACOMP0 / input | USB detect, HBN wake (test035) |
| IO10-IO15 | SDIO D2, D3, CMD, CLK, D0, D1 (10 k up to 3V3) | SDU / parked | test014 lines, test015 speed; parked (analog) while 3V3 off |
| IO16, IO17 | Y3 32.768 kHz crystal | XTAL32K | oscillates (test032) |
| IO20 | LEAK_SENS (1 M / 1 M, C109) | ACOMP1 / ADC0 | dry ~1.65 V, wet, HBN wake (test034) |
| IO21, IO22 | UART0 to K230 UART1 (R34/R36 up to 3V3) | UART / parked | test010, test018; parked while 3V3 off |
| IO27 | BOOT1 via RBT1 10 k | output | NAND boot low (reads 0.5 V parked: v2 item 7), USB boot high |
| IO28, IO29 | AHT20 I2C (R73/R74 4.7 k to 3V3_SLEEP) | I2C | reads (pt_i2c), idle high |
| IO30 | K230_PWR (R51, R19 100 k down) | output | rails on/off, low in HBN (R19) |
| EN | AI_EN (R38, PR1.5) | chip enable | reset from PR1 |
| D+, D- | PR1 USB | USB console | enumerates |
| NC pins | - | unused | defined state, no float (pull or analog) |

## Setup

To fill: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

BL616 GPIO (all pins), board_pins.h; product init code.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Configuration: each pin's function, pull, drive strength and IO voltage domain (3V3_SLEEP) in both firmwares
   (test and product) against this table: a script over board_pins.h and the init code, like check_pad_voltage.py.
2. Levels with a meter or the BL616's own read-back, in each power state: K230 rails off, rails on in reset, K230
   running, BL616 in HBN (test031).
3. Drive and read back where the other end is reachable (RSTN, K230_PWR via the rails, BOOT1 via the K230 ROM's
   boot path, UART and SDIO done in test010/014).
4. Back-feed: with the K230 rails off, no pin toward the K230 above ~0.1 V and the switched 3V3 at 0 V (firmware/sim
   predicts the cost of a mistake).

## Pass

Every pin as in the table in every state; no back-feed; unused pins in a defined state.

## Results

Not run yet.

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - each pin's level (table), 3V3 switched rail, VBAT C15.1.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |

## Boot time

n/a for the pins themselves; the pin init time at boot is part of test021.

## Battery energy

A floating or back-feeding pin shows as extra sleep current: compare test031 before and after any pin change.

## Obstacles

None yet.
