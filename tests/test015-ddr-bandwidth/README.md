# test015: K230 DDR bandwidth

Status: **planned** (2026-10-21).

DDR runs at 2667 MT/s (SPL training, test006), 64 MB. Bandwidth decides how much the KPU, the encoder and the
camera pipeline can do at once.

## Steps

1. A temporary MSH command (SDK copy, as the test014 bench; waits sleep, skill k230-rtsmart-hangs): `memcpy`,
   read-only (sum) and write-only (`memset`) over 1, 4, 16 MB blocks, cached; timed with `rdtime` (27 MHz).
2. Same through the uncached alias (DMA-visible), as the KPU/VENC see it.
3. Same while the KPU runs the change net in a loop (test012) and while the encoder runs (test026).

## Pass

Recorded MB/s per case; the drop under concurrent load; no errors (compare with test024's stress).


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| DDR write / read / copy; CPU clock | not measured yet | LPDDR4 2667 MT/s x 16 bit = 5.3 GB/s peak; CPU 1.6 GHz | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

The DDR clock's effect on boot: training time at 2667 vs 3200 MT/s (SPL), from test021's timestamps.

## Battery energy

Battery current during each bench (copy, read, write) vs idle; energy per GB moved. With test024 at 3200 MT/s: does the faster clock cost more per GB or save by finishing sooner?
