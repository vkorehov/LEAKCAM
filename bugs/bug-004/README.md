# bug-004: no battery measurement on the BL616 (the cell drains until its protection trips)

Found 2026-10-20 (firmware review). Board: MIFA rev 1 (fab 11a6a38).

## Problem

The BL616 decides when the K230 runs, but cannot measure the battery. The only divider (R59 200 k / R61 100 k,
VBAT/3) goes to K230 ADC_1 through Q1, switched by the K230's own rail, so a reading exists only while the K230
runs, and the firmware must start the K230 to learn the battery is too low to start it. The test firmware has
no reading at all.

Nothing on the board stops the drain: the BQ24072 has no discharge cutoff (on battery its BAT-FET stays on), and U5
(TPS63802, EN on VBAT) keeps 3V3_SLEEP up down to ~1.8 V. A board left on battery (rails on in the test
firmware, or a product unit at the end of its charge) drains the cell until its protection board trips (~2.4 V):
accepted as the guard (Status).
Recharging from there is slow or fails: below VBAT(SC) 1.6-2.0 V the charger sources only ~7.5 mA, below VLOWV
2.9-3.1 V it pre-charges at 10 % of the charge current.

## Fix

- **The cell's protection board is the guard** (Status): no battery sense on the BL616, on rev 1 or rev 2.
- **Rev 1: no hardware rework** (user, 2026-10-21: too dense to bodge by hand). Firmware workaround, BL616
  product firmware, not yet written:
  - the K230 reports its own reading (ADC_1) back to the BL616 in WAKE's ACK every session; the BL616 keeps it
    and the lockout flag in HBN RAM (aon_state.c: survives every hibernate, lost only with the battery);
  - below 3.10 V under load the BL616 cuts the K230 at once and hibernates with only the USB wake (no RTC, no
    leak wake); the lockout ends with USB plugged and the next reading >= 3.50 V;
  - 3.10 V under load is ~3.2 V at rest: ~2-3 % of the charge left, above VLOWV, so a recharge starts at full
    current;
  - the test firmware has no reading at all: on battery with no terminal on PR1 it switches the K230 rails off
    after 10 min and hibernates (a board left on the bench cannot drain the cell).

## Status

**Closed 2026-11-01, accepted (user decision): no divider on the BL616, neither rework nor rev 2.** The battery's
lowest voltage is the cell's protection board: the 503450 cell (34 x 50 x 5 mm, 1000 mAh) has a built-in one
(EXTERNAL_PARTS.md; typically a DW01-type IC with an 8205 dual FET), which cuts discharge at ~2.4 V and lets charging
back in. Checked in the BQ24072 datasheet (SLUS810K): the charger has no battery undervoltage cutoff of its own (its
UVLO is on IN; on battery OUT is connected to VBAT; the block diagram has no discharge comparator), so the protection
board is the guard. When it cuts, the rails collapse and the BL616's brown-out (the SDK's startup enables it: 2.40 V,
reset) keeps the chip from running erratically; the next USB plug recharges it (precharge at 10 % below ~3 V).
- The setup firmware (DESIGN.md 5.3.1, never starts the K230) has no cutoff of its own.
- The full firmware keeps the 3.10 V cutoff on the K230's reading as the gentler stop during sessions.
- Cost: a board that waits out its whole charge goes to ~2.4 V, which costs the cell some cycle life.
- To confirm on the next assembled board: tests/test038 step 7 (where the current really stops: the protection
  board's trip, measured).

Before (2026-10-20): a battery divider on the BL616 was planned for rev 2; dropped.

## Seen in bring-up (2026-10-21)

First thought to be a drained cell (burned ammeter fuse, board on the cell with the K230 at ~200 mA): it was the
cell's open solder joint on JB1, the cell read 3.69 V (tests/test001, test023). No drain or PCM trip has been seen
yet. The bug stands: nothing on the board stops a drain, and the pack's PCM is no 3.0 V guard (the cell sat at 2.9 V
on 2026-10-19 without a cut; generic PCMs trip ~2.4-2.5 V, deeper than the cell should go). The test firmware has no
cutoff: on battery, keep sessions short or watch VBAT.

