# test008: minimal image - the fewest bits that still boot and pass

Status: **in progress** (2026-10-23): builds 1+2 adopted (app init 0.484-0.494 s); kernel stays -O1; programs
fastest at -O3 LTO (cpubench sum 314.6 vs 344.5 ms at -Os), not adopted until the firmware rewrite; map-file and
BL616 steps open.

**Runs first after board bring-up** (after test007, the first RT-Smart boot): the floor every later test builds its
`## Needs` on.

The standing practice (tests/README) keeps every build minimal; this test pushes it to the floor on purpose: remove
everything no test's `## Needs` names, then keep removing until something breaks, and record where the floor is
and what each removal saved. Every byte on the NAND is read at each wake (UFFS reads every used page at mount,
test022); every kernel feature costs boot and RAM; the BL616's image is read at every 10-minute wake.

## Setup

K230: the bench image built by `firmware/k230_board/install.sh` + `leakcam_docker_build.sh`, flashed with
`flash_nand.sh`; K230 console `/dev/ttyACM1` (the BL616's port, test039), `boot_times`. BL616: `bl616_powertest` on the board (`make` /
`make flash`, ISP); the product firmware is only built and sized, never flashed. Meter in USB-C 1 VBUS (10 A range, cell full).

## Needs (build features and dependencies)

The union of every test's `## Needs`: this test's own list is the result.

## Steps

**K230:**
1. Union of all `## Needs`; every enabled kernel option (145 `=y` on 2026-10-22, 47 set in k230d_leakcam_defconfig),
   driver, component and linked library not in it: off, one group per build.
2. Flags: kernel `-O1 -g` -> `-Os` (and `-O2` for comparison), no debug info; programs `-ffunction-sections
   -fdata-sections`, LTO, strip; nncase/MPP only in the programs that use them.
3. leakcam_wake (5.18 MB static): map file by object/library; drop what the wake path does not call.
4. /bin and /sdcard: only the files the product reads.
5. Keep going past "needed" until the first failure (boot, a test's function): the floor, and the failing piece
   named.

**BL616:**
6. The product and test firmware: every `CONFIG_` and SDK component vs the `## Needs` union (shell and logs off in
   the product, BLE provisioning only, lwIP minimal: the K230 owns IP), flags (`-Os`, sections, LTO already on).
7. Wake path in TCM, the rest XIP; image size and the humidity slice's awake time.

**Then per test, from the floor:** the floor image + that test's `## Needs` only, built, flashed, the test run: the
difference to the floor is the cost of that test's features (bytes, boot ms, free RAM, current at idle), recorded
in the test's Metrics ("Features over the minimal image") and summed here per feature. The product image is the
floor + the union of the product tests' needs; a feature whose cost is high gets questioned first.

**Every build:** sizes (kdimg, rtthread.bin, each program, /bin and /sdcard used; BL616 .bin), boot to app init (3
cold boots), free RAM at msh, the speeds the change touches (test013/016/017/027), current at msh idle; the
tests whose part changed re-run.

## Pass

A table from the baseline to the floor: each removal with its saving in bytes and boot ms, the floor image named,
and the product image set to the smallest one that passes every test's `## Needs`.

## Result

- Builds 1+2 in the image (2026-10-22): defaults no test needs and CanMV leftovers off (VG_LITE, UART2/3/4,
  I2C_BITOPS, AF_UNIX, lwIP IGMP/RAW/loopback/ping, NETDEV and WLAN msh commands, OVERFLOW_CHECK, MPP FFmpeg,
  MPP_DSI_ENABLE_VIRT; memtest/venctest out). Options =y 145 -> 127; rtthread.bin 3 712 016 B (kernel -O1 with the
  KPU fix), /sdcard programs 6 616 314 B, kdimg 45.38 MB. App init 0.484 / 0.494 / 0.493 s (from 0.506 s); ADC,
  I2C, RTC, NAND 80 MHz (20.6 MB/s, 0 errors) unchanged (Build 1 + 2 on the board).
- Kept because the SDK references them: RT_DEBUG, RT_USING_PROC + DFS_PROCFS, RT_USING_USAGE (2026-10-22, build 1).
- Kernel stays at the SDK's -O1 (user: safe side). The KPU bandwidth fix (install.sh, mapped address) is kept at -O1
  (2026-10-22, Flags A).
- Programs: flags move leakcam_wake by 0.2 % at most (nncase and MPP are most of it). On the board -O3 LTO is the
  fastest: cpubench sum 314.6 ms vs 344.5 ms at -Os (-9 %), reduce x2 6.26 vs 21.07 ms (vectorised,
  `-march=rv64imafdcv`); +11.7 KB leakcam_wake, +127 KB leakcam_stream. Rule: adopt -O3 -flto; the compress step
  (miniz level 3, ~275 ms) dominates (2026-10-23, Flags B on the board).
- Open: -O3 -flto not in the product programs yet (rebuilt with the firmware rewrite).
- Open: kernel -O2 - leakcam_agent faults returning to the signal-return trampoline (0xfffffffffffff000), latent
  LWP signal-path bug; whether -O2 is faster was never measured.
- Open: MPP_ISP_CONFIG_INSTALL_BIN and _JSON_XML both on (ISP set twice in /bin; test020/030); FAST_BOOT_CONFIGURATION
  (rtapp.elf.gz) as a speed candidate for leakcam_agent/wake.
- Open: steps 3-5 (leakcam_wake map file, /bin and /sdcard pruning, the floor) and BL616 steps 6-7.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.47 / 0.47 s (median / max of 5; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.46, msh 0.47); longest step to mounted 0.21 s (2026-10-30, run.py) | test022 | per build |
| This part's boot share | n/a: the whole boot is this test's subject (test022 splits it) (2026-10-30) | - | mounts follow used pages |
| Test run time | 71 s automated (run.py, 2026-10-30), no setup | - | build + flash per variant |
| Speed / rate against the top | msh 0.47 s after pt_run in 5 of 5 boots (2026-10-30): no regression against builds 1+2 (app init 0.484-0.494 s) | SPEEDS.txt | no regression |
| Current floor / typical / peak | K230 at msh idle (agent running) 0.220 A at 5 V (USB-C 1 VBUS meter, test006, 2026-10-25); the image changes move it below the meter's 1 mA step | floor (test031) | |
| Voltage under load (min) | VBAT 4.40 V min (K230 ADC ch 1, test023 calibration, 5 samples; on USB: the charger rail, not the cell) (2026-10-30) | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: the image is not an operation; its boot energy is test022's | - | |
| Per day | n/a: through the boot (test022) and the sessions (test102) | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | die 44.0 / 46.1 / 46.1 C; board 36.9 / 37.2 / 37.2 C (AHT20) (2026-10-30) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 131124 / min 105196 / 131124 B, blocks 106 -> 106, deleted 0; K230 heap available 7025152 B steady; leak: n/a, 5 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-30) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 45.38 MB, rtthread.bin 3 712 016 B, /sdcard programs 6 616 314 B (builds 1+2, 2026-10-22); BL616 product / test firmware 1.27 MB / 121 KB (2026-10-21) | minimal image (rule) | per build |
| Compile flags | kernel `-O1 -g`; programs `-Os`, sections, `--gc-sections`, strip, no LTO; BL616 `-Os -g`, sections, `--gc-sections`, `-flto` (2026-10-22) | smallest code (rule) | per build |
| Features over the minimal image | n/a: this test is the floor; the union of all tests' Needs is steps 3-5 (open) | - | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | programs -O3 -flto -march=rv64imafdcv fastest: cpubench 314.6 vs 344.5 ms at -Os (-9 %), +11.7 KB leakcam_wake (Flags B); kernel stays -O1 (-O2 faults, Flags A) | the faster one stays | |

## Boot time

The test's main result next to size: boot to app init per build.

## Battery energy

Energy per wake per build (ENERGY.txt section 3): the gain in uAh per wake and per month.
