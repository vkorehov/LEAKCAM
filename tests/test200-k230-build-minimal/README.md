# test200: K230 image: maximum compiler optimisation, only the needed OS features

Status: **planned** (2026-10-22).

Every byte on the NAND is read at every wake (UFFS mounts read the tags of every used page: test021) and every
kernel feature costs boot and RAM. Rule: one firmware version, as light as possible (memory: single version,
lightest). This test makes the K230 image as small and fast as the product allows, change by change, each one
measured.

## Setup

Bench and product image built from `firmware/k230_board/install.sh` + `leakcam_docker_build.sh`; the baseline
image of 2026-10-22 (NAND 80 MHz, /bin 8 MiB one ISP set, /sdcard 32 MiB). Board on USB-C 1 with the meter,
the CH340 console, `boot_times`.

## Needs (build features and dependencies)

To fill: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what test200/201 must keep).

## Steps

1. **Inventory.** Every enabled kernel option (`rtconfig.h` / `.config`: 145 `=y` on 2026-10-22, 47 set in
   k230d_leakcam_defconfig), every driver and component in the image, every library the programs link: each one
   with its user (which product function needs it) or marked unused.
2. **Kernel features off.** Unused drivers/components/shell commands/debug options off (e.g. unused buses, file
   systems, network services, logging levels); one group per build, so a regression points at one group.
3. **Compiler and linker.** Programs: `-Os` vs `-O2` (speed-critical parts: KPU pre/post-processing, imgdiff),
   `-ffunction-sections -fdata-sections` with the existing `--gc-sections`, LTO (`-flto`), `strip`; kernel the
   same where the SDK allows. Check that the nncase / MPP libraries are only linked where used.
4. **Programs.** `leakcam_wake` (5.18 MB, static): what makes it big (`size`, map file: nncase runtime, MPP,
   miniz, opus); split or link only what each program needs; test programs (memtest, venctest, bench) out of the
   product image.
5. **Measure every change:** sizes (rtthread.bin, each program, /bin and /sdcard used), boot time (`boot_times`,
   3 cold boots), free RAM at msh (`free`), the speeds the change touches (test015/016/012/026), current at msh idle.

## Pass

Every enabled feature has a named user; no unused test program in the product image; each change kept only if
size or boot time improves and no speed or function regresses (the tests it touches re-run).

After the optimisation: the tests whose part changed run again (user, 2026-10-22), each with its full metrics, so
the gain and any regression show per part.

## Results

Not run yet.

### Baseline 2026-10-22

| Part | Size | Note |
|---|---|---|
| rtthread.bin (kernel) | 3.75 MB | 145 options =y |
| /bin | 0.68 MB | one ISP set (1280x960) |
| /sdcard programs | 6.76 MB | leakcam_wake 5.18 MB, leakcam_stream 1.03 MB, kmodel 0.21 MB, hist 0.10, agent 0.09, venctest 0.08 (test) |
| programs' flags | -Os, --gc-sections | no -ffunction-sections, no LTO |
| boot to app init | 0.506 s | test021 |

## Metrics (to fill on every run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load.

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | to fill | test021 | before / after each change |
| This part's boot share | to fill | - | mount time follows used pages |
| Test run time | to fill | - | build time per variant |
| Speed / rate against the top | to fill | SPEEDS.txt | no regression allowed |
| Current floor / typical / peak | to fill | floor (test030) | |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | to fill | - | |
| Per day | to fill | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | to fill | K230 limit (datasheet) | chip_temp, AHT20 |
| Image (sizes) | kdimg 45.4 MB (/sdcard 32 MiB, /bin 8 MiB one ISP set), kernel 3.75 MB, /bin 0.68 MB, /sdcard programs 6.76 MB | test200 / test201 | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, --gc-sections (no -ffunction-sections, no LTO) | test200 / test201 | |

## Boot time

The point of this test: boot time before / after each change, with its size.

## Battery energy

Smaller images mount and load faster: the energy per wake before / after each change (ENERGY.txt section 3).

## Obstacles

None yet.
