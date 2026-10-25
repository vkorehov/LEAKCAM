# test007: RT-Smart boot, 2026-10-18

Status: **pass; PMU RTC with R45 bridged (bug-003)** (2026-10-18/20).

From NAND: SPL -> OpenSBI -> RT-Smart, UFFS on /bin and /sdcard, the agent started from /sdcard/app.

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

RT-Smart kernel: PMU (RT_USING_RTC_PMU, 72-pad table), UFFS mounts (/bin, /sdcard), msh, the agent.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| Kernel after `OpenSBI v0.9` | hung at pin 64 / RTC init: [bug-003](../../bugs/bug-003/README.md) (PMU) | fixed (R45 bridged) |
| Kernel boot | fault in the AI2D driver log | fixed (log off) |
| /bin, /sdcard | not mounted by the SDK | fixed (UFFS mounts) |
| /sdcard | mount failures, random bad blocks | fixed (busy-wait patch) |
| Boot after the fixes, 3 reboots + a final flash | `msh />`, UFFS mounted, agent starts, zero NAND errors | pass |

**Pass.**

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Automated run 2026-10-25 (run.py, 97 s): pass

5 of 5 cold boots to msh with /bin and /sdcard mounted.

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)

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

## Obstacles

- **Silent after "OpenSBI v0.9"** -> OpenSBI is quiet; the kernel hung -> found with temporary markers
  (`rt_hw_console_output`, prints via OpenSBI) in the SDK, reverted after.
- **Hang at pin 64 / RTC** -> INT4 at 0.3 V (R45 100 k against the pad's ~20 kOhm pull-down), the PMU never wakes ->
  R45 bridged (INT4 = 1V8); the PMU RTC is used (2026-10-20, [bug-003](../../bugs/bug-003/README.md)).
- **Link error** -> the pins.py footer cut also removed the camera-LED init -> restored.
- **Fault in AI2D driver** -> Canaan's `ai_2d_log()` drops its arguments -> install.sh turns that log off.
- **/sdcard empty, agent not found** -> the SDK never mounts the NAND -> install.sh adds UFFS mounts (nand0 /bin, nand1 /sdcard).
- **/sdcard mount fails, random "bad blocks", "can't find right page"** -> Canaan's `spinand_isbusy()` abandons a
  page read when its confirm status read still shows BUSY (0x01); UFFS takes the -1 as "ECC corrected", uses
  stale buffers and marks good blocks bad -> install.sh replaces it (`rtsmart/patches/spinand_isbusy.c`: 20 ms
  real-time limit, two "ready" reads in a row). Found with `nandtest` (1000 clean repeat reads) + a status print.
- **Leftover blocks after re-flashing** -> the burn loader does not erase what an image does not cover -> `flash_nand.sh` erases first.
- **OOB layout** -> checked, fine: the loader puts the image's tag at OOB 4-12, where the kernel reads it.
- **"Too many pending bad blocks"** -> side effect of the busy-wait bug; gone.
- **"config command failed (-1)"** -> `config.txt` asks for the SD-card resize -> gen_config writes `board=leakcam`.

None in the run of 2026-10-25.
