# test034: leak probe wake (ACOMP1) and wet/dry thresholds

Status: **pass with opens** (2026-10-22): the product's leak threshold was wrong (1.65 V = the dry level: a dry probe read WET), fixed to 0.85 V; S1/S2/S3 shorts detected, settled 250 mV; HBN wake on going wet and on going dry works. Open: real water types, resistor sweep, 24 h no-false-wake.

The leak probes S1-S3 pull LEAK_SENS (1 M / 1 M divider from 3V3_SLEEP, 1.65 V dry) down when water bridges them;
ACOMP1 wakes the BL616 from hibernate on that edge. The product's first job.

## Setup

BL616 test firmware: the leak comparator exactly as the product's `leak_init()` (ACOMP1 on ADC0 = IO20, 30 mV
hysteresis, slowest bias); `pt_leak` (node mV by the ADC, comparator state, last wake source), `pt_leak watch [s]`
(one line per comparator change, ADC read after the node settles), `pt_leak sweep` (comparator output against the
node for every reference setting), `pt_sleep <s>` arms the HBN wake on the next comparator edge as `hbn_sleep()`.
Probe sockets shorted by hand (wire across S1/S2/S3). Circuit (SENS.SchDoc): R71 1 M from 3V3_SLEEP and R72 1 M + C109
100 nF to GND on LEAK_SENS; each socket between two 47 k (S1: RB1 to LEAK_SENS, RB2 to GND; S2 RB3/RB4; S3 RB5/RB6).

## Needs (build features and dependencies)

BL616 ACOMP1 (leak), ADC (pt_leak), HBN wake on ACOMP.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Probe resistance sweep across S1 (resistor decade or fixed values 10 k .. 10 M): LEAK_SENS voltage (pt_leak) and
   the comparator's state; the wet/dry threshold in ohms.
2. Real water: tap water, distilled, salt water, a wet towel on the probe tips: resistance and detection.
3. Wake from HBN (test firmware pt_sleep, product firmware): a wet edge wakes within ms; drying (the reverse edge)
   too, as the product expects.
4. All three sockets S1, S2, S3; a probe with a broken wire (open) stays dry.
5. Noise: no false wake over 24 h dry (LED drivers, Wi-Fi bursts on the same rail).

## Pass

Wet detected at every water type the product must see, dry never woken falsely in 24 h; wake latency recorded.

## Results

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
24 h dry without a false wake (step 5; the product's evlog will count wakes); the product firmware build (rewrite).

## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - battery lead, USB out (wake from sleep); voltage - LEAK_SENS (R71/R72 node), 3V3_SLEEP.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test022) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| wake latency on wet | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | to fill | minimal image (rule) | the image this run used |
| Compile flags | to fill | smallest code (rule) | |
| Features over the minimal image | to fill: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

HBN wake on a leak to the K230's power-on (with test021): the first part of the leak-to-video time.

## Battery energy

The divider's current wet vs dry (1.65 uA dry; wet up to 3.3 uA): a standing leak raises the sleep current.

## Obstacles

- **The first `pt_leak watch` read 692 mV / 265 kohm for a hard short** -> it read the ADC 2 ms after the comparator
  flipped, while C109 was still discharging (tau 8.6 ms) -> waits 50 ms (wet) / 250 ms (dry) before the reading.
  The user pointed at the series resistors in the schematic; they were already in the formula (94 k).
- **Holding a short while clicking "connect to Linux"** broke the contact for a moment: the dry-again test woke on that.

