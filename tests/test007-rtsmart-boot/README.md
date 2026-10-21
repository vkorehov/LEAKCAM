# test007: RT-Smart boot, 2026-10-18

From NAND: SPL -> OpenSBI -> RT-Smart, UFFS on /bin and /sdcard, the agent started from /sdcard/app.

| Step | Result | |
|---|---|---|
| Kernel after `OpenSBI v0.9` | hung at pin 64 / RTC init: [bug-003](../../bugs/bug-003/README.md) (PMU) | fixed (R45 bridged) |
| Kernel boot | fault in the AI2D driver log | fixed (log off) |
| /bin, /sdcard | not mounted by the SDK | fixed (UFFS mounts) |
| /sdcard | mount failures, random bad blocks | fixed (busy-wait patch) |
| Boot after the fixes, 3 reboots + a final flash | `msh />`, UFFS mounted, agent starts, zero NAND errors | pass |

**Pass.**



## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: OpenSBI -> RT-Smart banner -> msh (now part of test021).

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: superseded by test021 (boot energy) and test101 (session).

## Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (to fill on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | test021 | longest step: |
| This part's boot share | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | |
| Test run time | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | not measured: run before the metrics rule (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | not measured: run before the metrics rule (2026-10-21); fill on a re-run | floor (test030) | at the probe above |
| Voltage under load (min) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured: run before the metrics rule (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | not measured: run before the metrics rule (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured: run before the metrics rule (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |

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
