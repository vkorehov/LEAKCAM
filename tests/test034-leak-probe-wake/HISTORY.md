# test034: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Status line before the 2026-10-29 cleanup

Status: **pass with opens** (2026-10-22): the product's leak threshold was wrong (1.65 V = the dry level: a dry probe read WET), fixed to 0.85 V; S1/S2/S3 shorts detected, settled 250 mV; HBN wake on going wet and on going dry works. Open: real water types, resistor sweep, 8 h no-false-wake.

### 2026-10-22

**Threshold bug (product):** dry probe, node 1648 mV, comparator **WET**. `pt_leak sweep` against the dry node:

| scaling factor | output for vio_sel 0 .. 63 (1 = node above the reference) |
|---|---|
| 0.25, 0.5, 0.75, 1 | all identical: 1 for vio_sel 0-32, 0 from 33 |

So the reference is **vio_sel x 50 mV and the scaling factor does nothing** on this input. The product's vio_sel 33 x
0.5, meant as 0.825 V, was 1.65 V, the dry level itself: on the board the product would have seen a leak at every
wake (a K230 session, every time, until the battery is flat). Fixed: vio_sel 17 = **0.85 V** (wet below ~430 kohm
across a probe), leak_wake.c, the test firmware, the K230's PROBE_WET_MV 825 -> 850, DESIGN.md, firmware/README.md.
The USB comparator (ACOMP0, vio_sel 33 x 1 = 1.65 V) was right by luck of its scaling 1.

| Test | Result |
|---|---|
| dry | 1648-1650 mV, comparator dry |
| hard short S1, S2, S3 (in turn, by hand) | WET each, settled **247-256 mV** (calculated 262 mV: 94 k to GND against 1 M), = 0 kohm |
| hand-held contact | bounces: several wet/dry changes within ~1 s per touch, the comparator follows each (30 mV hysteresis) |
| HBN, armed for going wet, S1 shorted during the 120 s sleep | woke **by leak**, console back 47 s after the sleep started (short + VMware click), not at 120 s |
| HBN, slept while wet (armed for going dry), contact lost for a moment while clicking | woke **by leak** after ~10 s, read wet again (contact restored): the dry edge wakes it |
| current, rails off, BL616 awake, comparator on | 62 mA (63 mA before: the comparator is not visible at this resolution) |

Wake latency (wet edge -> main()): not measured directly (no edge timestamp on the board); from parts: C109 falls
from 1.65 V to the 0.85 V trip in tau x ln(1.39 / 0.59) = 8.6 ms x 0.86 = **7.4 ms** for a hard short (slower for
water), then the HBN wake path **~15 ms** (test021: 12-16 ms) = ~22 ms to main().

Open: real water (tap, distilled, salt, wet towel) and a resistor sweep 10 k .. 10 M for the threshold in ohms;
8 h dry without a false wake (step 5; the product's evlog will count wakes); the product firmware build (rewrite).

## Obstacles

- **The first `pt_leak watch` read 692 mV / 265 kohm for a hard short** -> it read the ADC 2 ms after the comparator
  flipped, while C109 was still discharging (tau 8.6 ms) -> waits 50 ms (wet) / 250 ms (dry) before the reading.
  The user pointed at the series resistors in the schematic; they were already in the formula (94 k).
- **Holding a short while clicking "connect to Linux"** broke the contact for a moment: the dry-again test woke on that.

## Real water by hand: 2026-10-31

The user dipped the probe on cue; the host read `pt_leak` 5 times per point (scratch reader, voltages only at the
user's request: "record voltages not resistance"). Results in README Result; raw lines in water_2026-10-31.log.

Obstacles:
- Two host logs started with the wrong label (the probe was out of the water when "salted water" was logged): the
  user said so, the label in the log reads "salted water then drying", the probe was in air from 17:41.
- VM frozen 505 s (17:45-17:53, vmtoolsd "interval longer ... actual=505 sec"): the Mac slept. Fix: `pt_leak log`
  (the BL616 samples into RAM, 1024 points; dump with `pt_leak log dump`); host sleep no longer loses points.
- My first readings converted the node to kohm; near a short the formula went negative (-8 kohm below the measured
  short level): dropped, voltages only.

## Salt residue: 2026-10-31 evening

Spot reads after the drying log: 657 mV (~19:00), 958, 1057-1075, 1516 mV (~19:43, nearly dry), then 376 mV (~19:46)
and 318-331 mV at 20:26 with brief jumps (409, 638 mV) while the user said the probe was dry. The probe unplugged
from the socket: 1651 mV, dry: the board is clean, the salt left in the probe conducts (user: "salt residue is
conducting").

Located 20:31-20:36: RF ruled out (BL616 rebooted, radio off: still 311 mV steady); wires desoldered: board 1648 mV;
wires resoldered by the user to probe-PCB pads with a wider trace gap: 1647 mV, dry. The salt film bridged the narrow
trace gap on the probe PCB at the wire joints.
