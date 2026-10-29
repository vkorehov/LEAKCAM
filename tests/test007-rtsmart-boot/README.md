# test007: RT-Smart boot

Status: **pass** (2026-10-25): 5 of 5 cold boots from NAND to msh, /bin and /sdcard mounted, agent starts; PMU RTC
with R45 bridged (bug-003).

From NAND: SPL -> OpenSBI -> RT-Smart, UFFS on /bin and /sdcard, the agent started from /sdcard/app.

## Setup

The image flashed to NAND (`flash_nand.sh`, erase first), NAND boot from the BL616 test firmware; R45 bridged
(bug-003). Automated: run.py, 5 cold boots. Probes: see Metrics.

## Needs (build features and dependencies)

RT-Smart kernel: PMU (RT_USING_RTC_PMU, 72-pad table), UFFS mounts (/bin, /sdcard), msh, the agent.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Cold boot from NAND: SPL -> OpenSBI -> RT-Smart -> msh.
2. /bin and /sdcard mounted, no NAND errors, the agent starts from /sdcard/app.
3. Repeat (run.py: 5 cold boots).

## Pass

Every boot reaches `msh />` with UFFS mounted, the agent started and zero NAND errors.

## Result

- Boots from NAND to `msh />`, UFFS mounted, agent starts, zero NAND errors (3 reboots + a final flash, 2026-10-18);
  5 of 5 cold boots (run.py, 2026-10-25).
- The boot needs, in the image / on the board: R45 bridged (bug-003, PMU wakes, PMU RTC used, 2026-10-20); Canaan's
  AI2D log off; UFFS mounts added by install.sh (nand0 /bin, nand1 /sdcard); the `spinand_isbusy()` replacement
  (`rtsmart/patches/spinand_isbusy.c`); `flash_nand.sh` erasing before writing; gen_config `board=leakcam`
  (2026-10-18).

Open:
- Image sizes and compile flags of this test's image not recorded.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.45 / 0.45 s (median / max of 5; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.44, msh 0.45); longest step to mounted 0.19 s (2026-10-25, run.py) | test022 | longest step: |
| This part's boot share | OpenSBI -> msh 0.21 / 0.21 s (median / max of 5) (2026-10-25) | - | |
| Test run time | 97 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | n/a: boot step; mount speed is in the boot times (test022) (2026-10-25) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | 0.220 / 0.220 / 0.220 A at 5 V (USB-C 1 VBUS meter; K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)) (2026-10-25) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.35 V min (K230 ADC ch 1, test023 calibration, 5 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 220 mA x 4.35 V = 958 mW; per cold boot to msh (0.45 s) 0.43 J, 28 uAh (2026-10-25) | - | scripts/energy.py |
| Per day | 4 x cold boot to msh (K230 sessions, ENERGY.txt): 0.11 mAh/day, 0.17 % of 2000 mAh per month (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 40.7 / 41.3 / 39.8 C; board 33.3 / 33.3 / 32.5 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 206440 / min 204396 / 206440 B, blocks 16 -> 16, deleted 0; K230 heap available 7025152 B steady; leak: n/a, 5 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | n/a: the kernel boot is the floor itself (test008) (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | kernel stays -O1: -O2 faults in the LWP signal return (test008 Flags A, 2026-10-22), kept -O1 by the user's decision; this result is kernel/driver code, no faster build to compare until that is fixed (2026-10-25) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: OpenSBI -> RT-Smart banner -> msh (now part of test022).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: superseded by test022 (boot energy) and test101 (session).
