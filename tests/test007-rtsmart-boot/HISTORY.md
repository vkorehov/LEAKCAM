# test007: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 97 s): pass

5 of 5 cold boots to msh with /bin and /sdcard mounted.

- meter K230 at msh idle (agent running): 0.220 A (read in test006-k230-nand-boot-spl)

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### 2026-10-18 (title "RT-Smart boot, 2026-10-18")

| Step | Result | |
|---|---|---|
| Kernel after `OpenSBI v0.9` | hung at pin 64 / RTC init: [bug-003](../../bugs/bug-003/README.md) (PMU) | fixed (R45 bridged) |
| Kernel boot | fault in the AI2D driver log | fixed (log off) |
| /bin, /sdcard | not mounted by the SDK | fixed (UFFS mounts) |
| /sdcard | mount failures, random bad blocks | fixed (busy-wait patch) |
| Boot after the fixes, 3 reboots + a final flash | `msh />`, UFFS mounted, agent starts, zero NAND errors | pass |

**Pass.**

### Superseded status line

- Status: **pass; PMU RTC with R45 bridged (bug-003)** (2026-10-18/20).

## Obstacles

- **"config command failed (-1)"** -> `config.txt` asks for the SD-card resize -> gen_config writes `board=leakcam`.
- **"Too many pending bad blocks"** -> side effect of the busy-wait bug; gone.
- **OOB layout** -> checked, fine: the loader puts the image's tag at OOB 4-12, where the kernel reads it.
- **Leftover blocks after re-flashing** -> the burn loader does not erase what an image does not cover -> `flash_nand.sh` erases first.
- **/sdcard mount fails, random "bad blocks", "can't find right page"** -> Canaan's `spinand_isbusy()` abandons a
  page read when its confirm status read still shows BUSY (0x01); UFFS takes the -1 as "ECC corrected", uses
  stale buffers and marks good blocks bad -> install.sh replaces it (`rtsmart/patches/spinand_isbusy.c`: 20 ms
  real-time limit, two "ready" reads in a row). Found with `nandtest` (1000 clean repeat reads) + a status print.
- **/sdcard empty, agent not found** -> the SDK never mounts the NAND -> install.sh adds UFFS mounts (nand0 /bin, nand1 /sdcard).
- **Fault in AI2D driver** -> Canaan's `ai_2d_log()` drops its arguments -> install.sh turns that log off.
- **Link error** -> the pins.py footer cut also removed the camera-LED init -> restored.
- **Hang at pin 64 / RTC** -> INT4 at 0.3 V (R45 100 k against the pad's ~20 kOhm pull-down), the PMU never wakes ->
  R45 bridged (INT4 = 1V8); the PMU RTC is used (2026-10-20, [bug-003](../../bugs/bug-003/README.md)).
- **Silent after "OpenSBI v0.9"** -> OpenSBI is quiet; the kernel hung -> found with temporary markers
  (`rt_hw_console_output`, prints via OpenSBI) in the SDK, reverted after.
