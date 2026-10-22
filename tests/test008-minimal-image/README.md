# test008: minimal image - the fewest bits that still boot and pass

Status: **planned** (2026-10-22). **Runs first after board bring-up** (after test007, the first RT-Smart boot): the
floor every later test builds its `## Needs` on.

The standing practice (tests/README) keeps every build minimal; this test pushes it to the floor on purpose: remove
everything no test's `## Needs` names, then keep removing until something breaks, and record where the floor is
and what each removal saved. Every byte on the NAND is read at each wake (UFFS reads every used page at mount,
test022); every kernel feature costs boot and RAM; the BL616's image is read at every 10-minute wake.

## Setup

K230: the bench image built by `firmware/k230_board/install.sh` + `leakcam_docker_build.sh`, flashed with
`flash_nand.sh`; CH340 console, `boot_times`. BL616: `bl616_powertest` and the product firmware, `make` /
`make flash` (ISP). Meter in USB-C 1 VBUS (10 A range, cell full).

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

## Results

Not run yet.

### Baseline 2026-10-22

| Part | Size | Note |
|---|---|---|
| kdimg | 45.4 MB | /sdcard 32 MiB, /bin 8 MiB |
| rtthread.bin (kernel) | 3.75 MB | `-O1 -g`, 145 options =y |
| /bin | 0.68 MB | one ISP set (1280x960) |
| /sdcard programs | 6.76 MB | leakcam_wake 5.18 MB, stream 1.03, kmodel 0.21, hist 0.10, agent 0.09, venctest 0.08 (test) |
| programs' flags | `-Os`, `--gc-sections` | no `-ffunction-sections`, no LTO |
| boot to app init | 0.506 s | test022 |
| BL616 product / test firmware | 1.27 MB (2026-10-21) / 121 KB | `-Os -g`, sections, `--gc-sections`, `-flto` |

## Metrics (to fill on every run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | to fill | test022 | per build |
| This part's boot share | to fill | - | mounts follow used pages |
| Test run time | to fill | - | build + flash per variant |
| Speed / rate against the top | to fill | SPEEDS.txt | no regression |
| Current floor / typical / peak | to fill | floor (test031) | |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | to fill | - | |
| Per day | to fill | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | to fill | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | baseline above | minimal image (rule) | per build |
| Compile flags | baseline above | smallest code (rule) | per build |
| Features over the minimal image | to fill: per test, its Needs on top of the floor | - | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | to fill: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

The test's main result next to size: boot to app init per build.

## Battery energy

Energy per wake per build (ENERGY.txt section 3): the gain in uAh per wake and per month.

## Obstacles

None yet.
