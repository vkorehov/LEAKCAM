# test034: leak probe wake (ACOMP1) and wet/dry thresholds

Status: **pass with opens** (2026-10-22): leak threshold vio_sel 17 = 0.85 V (wet below ~430 kohm); S1/S2/S3 shorts
detected (settled 247-256 mV); HBN wake on going wet and on going dry works. Open: real water types, resistor sweep,
8 h no-false-wake.

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
3. Wake from HBN (test firmware `pt_sleep`, comparators armed): a wet edge wakes within ms; drying (the reverse edge)
   too, as the product expects.
4. All three sockets S1, S2, S3; a probe with a broken wire (open) stays dry.
5. Noise: no false wake over 8 h dry (user 2026-10-23: 24 h overkill, 8 h overnight) (LED drivers, Wi-Fi bursts on the same rail).

## Pass

Wet detected at every water type the product must see, dry never woken falsely in 8 h; wake latency recorded.

## Result

- ACOMP1 reference on this input is **vio_sel x 50 mV; the scaling factor does nothing** (`pt_leak sweep`, factors
  0.25-1 all identical) (2026-10-22). Leak threshold therefore vio_sel 17 = **0.85 V** (wet below ~430 kohm across a
  probe), in leak_wake.c, the test firmware, the K230's PROBE_WET_MV 850, DESIGN.md, firmware/README.md (2026-10-22).
  The USB comparator (ACOMP0, vio_sel 33 x 1 = 1.65 V) is right (2026-10-22).
- Dry: 1648-1650 mV, comparator dry (2026-10-22).
- Hard short S1, S2, S3 (in turn): WET each, settled **247-256 mV** (calculated 262 mV: 94 k to GND against 1 M)
  (2026-10-22). Read the ADC only after the node settles: 50 ms (wet) / 250 ms (dry) (C109 tau 8.6 ms).
- Hand-held contact bounces: several wet/dry changes within ~1 s per touch, the comparator follows each (30 mV
  hysteresis) (2026-10-22).
- HBN armed for going wet: woke **by leak** on an S1 short during a 120 s sleep; HBN slept while wet (armed for going
  dry): the dry edge wakes it (2026-10-22).
- Comparator current not visible at the meter's resolution: rails off, BL616 awake, comparator on 62 mA (2026-10-22).
- Wake latency (estimated, not measured): C109 1.65 V -> 0.85 V trip **7.4 ms** for a hard short (slower for water),
  then the HBN wake path **~15 ms** (test021: 12-16 ms) = ~22 ms to main() (2026-10-22).

Open:
- Real water (tap, distilled, salt, wet towel) and a resistor sweep 10 k .. 10 M for the threshold in ohms.
- 8 h dry without a false wake (step 5; the test firmware's evlog counts the wakes).
- Wake latency not measured directly (no edge timestamp on the board).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - battery lead, USB out (wake from sleep); voltage - LEAK_SENS (R71/R72 node), 3V3_SLEEP.

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| wake latency on wet | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

HBN wake on a leak to the K230's power-on (with test021): the first part of the leak-to-video time.

## Battery energy

The divider's current wet vs dry (1.65 uA dry; wet up to 3.3 uA): a standing leak raises the sleep current.
