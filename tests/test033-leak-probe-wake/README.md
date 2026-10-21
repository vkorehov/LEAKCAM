# test033: leak probe wake (ACOMP1) and wet/dry thresholds

Status: **planned** (2026-10-21).

The leak probes S1-S3 pull LEAK_SENS (1 M / 1 M divider from 3V3_SLEEP, 1.65 V dry) down when water bridges them;
ACOMP1 wakes the BL616 from hibernate on that edge. The product's first job.

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

## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| wake latency on wet | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

HBN wake on a leak to the K230's power-on (with test020): the first part of the leak-to-video time.

## Battery energy

The divider's current wet vs dry (1.65 uA dry; wet up to 3.3 uA): a standing leak raises the sleep current.
