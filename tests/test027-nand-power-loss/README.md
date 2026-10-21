# test027: NAND / UFFS power-loss robustness

Status: **planned** (2026-10-21).

The product writes clips, logs and references to /sdcard (UFFS on NAND) while the battery can run out or the BL616
can cut the rails. UFFS is log-structured; this proves it on rev 1.

## Steps

1. A test hook writes files in a loop (sizes 1 KB .. 2 MB, fsync, verify the previous file).
2. BL616 test firmware cuts the rails at random moments (`pt_off` after a random 2-20 s, `pt_on`, `pt_run`), 100
   times, console recorded.
3. After each boot: mount OK, the files written before the last fsync intact, free space not shrinking.

## Pass

100 random power cuts: every mount succeeds, no fsynced file lost or corrupt, no space leak.


## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| recovery time after power loss | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

Mount time after an unclean power cut (UFFS recovery may scan more) vs a clean mount.

## Battery energy

n/a: a robustness test (the energy of writes is test016).
