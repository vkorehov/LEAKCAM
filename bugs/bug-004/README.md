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

- **Rev 1 rework: battery sense on PGOOD (IO3)** ([bat_sense_rework.png](bat_sense_rework.png), where:
  [bat_sense_board.png](bat_sense_board.png)). R23 (100 k PGOOD pull-up to 3V3_SLEEP) out; 680 k + 510 k in
  series across C50 (VBAT / GND), 100 nF from the junction to GND (required: the ADC samples from it, not through
  the divider's 290 k), the junction wired to R23 pad 2. PGOOD is open drain: with USB out IO3 reads VBAT x 0.429
  (4.25 V -> 1.82 V, 3.0 V -> 1.29 V; ADC 2.0 V reference, ~1 mV of battery per step), with USB in PGOOD pulls it
  to ~0 V. Costs 3.5 uA, only without USB. Independent of the K230's divider (both on VBAT, a source of
  ~0.1 Ohm). Check after the rework: BL616 reading vs a meter at two cell voltages (within ~20 mV; more means pin
  leakage through the 290 k: then 390 k / 300 k).
- **Rev 2:** the same divider by design (bugs/v2.txt item 8).
- **Firmware (BL616, product and test; needs the rework):** USB detect = IO3 below 0.5 V (ACOMP0, still the HBN
  plug wake). Battery from IO3 whenever USB is out. Before every K230 power-on the rest reading must be >= 3.30 V;
  during a session an averaged reading below 3.10 V for several seconds cuts the K230. Below the cutoff: K230 off,
  hibernate with only the USB wake (no RTC, no leak wake); the lockout ends with USB plugged and >= 3.50 V. Cutoff
  3.30 V at rest leaves ~2-3 % of the charge and stays above VLOWV, so a recharge starts at full current.

## Status

Rework drawn, not yet fitted; firmware not yet written. Until both are done, the old firmware reads USB wrong on
a reworked board (IO3 no longer reaches 3.3 V on battery).
