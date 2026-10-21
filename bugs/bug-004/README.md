# bug-004: no battery measurement on the BL616 (the cell drains until its protection trips)

Found 2026-10-20 (firmware review). Board: MIFA rev 1 (fab 11a6a38).

## Problem

The BL616 decides when the K230 runs, but cannot measure the battery. The only divider (R59 200 k / R61 100 k,
VBAT/3) goes to K230 ADC_1 through Q1, switched by the K230's own rail, so a reading exists only while the K230
runs, and the firmware must start the K230 to learn the battery is too low to start it. The test firmware has
no reading at all.

Nothing else stops the drain: the BQ24072 has no discharge cutoff (on battery its BAT-FET stays on), and U5
(TPS63802, EN on VBAT) keeps 3V3_SLEEP up down to ~1.8 V. A board left on battery (rails on in the test
firmware, or a product unit at the end of its charge) drains the cell until its protection board trips.
Recharging from there is slow or fails: below VBAT(SC) 1.6-2.0 V the charger sources only ~7.5 mA, below VLOWV
2.9-3.1 V it pre-charges at 10 % of the charge current.

## Fix

- **Rev 2:** battery sense on the BL616 (bugs/v2.txt item 8): PGOOD (IO3) pulled up to VBAT through 680 k with
  510 k to GND and 100 nF at IO3, instead of R23 (100 k to 3V3_SLEEP). PGOOD is open drain: with USB out IO3 reads
  VBAT x 0.429 (4.25 V -> 1.82 V, 3.0 V -> 1.29 V; ADC 2.0 V reference, ~1 mV of battery per step), with USB in
  ~0 V, so USB detection stays on the same pin (ACOMP0 threshold 0.5 V, still the HBN plug wake). 3.5 uA, only
  without USB. Independent of the K230's divider (both on VBAT, a ~0.1 Ohm source). The 100 nF is required: the
  ADC samples from it, not through the divider's 290 k.
- **Rev 1: no hardware rework** (user, 2026-10-21: too dense to bodge by hand). Firmware workaround, BL616
  product firmware, not yet written:
  - the K230's own reading (ADC_1, answered in WAKE's ACK every session) is the battery value;
  - below 3.10 V under load the BL616 cuts the K230 at once and hibernates with only the USB wake (no RTC, no
    leak wake); the lockout ends with USB plugged and the next reading >= 3.50 V;
  - 3.10 V under load is ~3.2 V at rest: ~2-3 % of the charge left, above VLOWV, so a recharge starts at full
    current;
  - the test firmware has no reading at all: on battery with no terminal on PR1 it switches the K230 rails off
    after 10 min and hibernates (a board left on the bench cannot drain the cell).

## Status

Open on rev 1: firmware workaround to write. Rev 2: divider in the schematic.
