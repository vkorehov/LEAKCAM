# test201: BL616 firmware: maximum compiler optimisation, only the needed SDK features

Status: **planned** (2026-10-22).

The BL616 wakes every 10 min for humidity and at every K230 session; its boot and awake time are battery
(ENERGY.txt section 3), and its flash image is read at every wake (XIP). Same rule as test200: only what the
product needs (memory: single version, lightest; BLE only for Wi-Fi credentials).

## Setup

Product firmware `firmware/bl616` (FreeRTOS, Wi-Fi, BLE, lwIP; 1.27 MB on 2026-10-21) and the test firmware
`bl616_powertest` (121 KB); built with `make` (container, host.mk), flashed with `make flash` (ISP). Meter in
USB-C 1 VBUS; for the awake time the BL616's own timestamps (`awake <ms>` in the hbn log line).

## Needs (build features and dependencies)

To fill: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what test200/201 must keep).

## Steps

1. **Inventory.** Every `CONFIG_` in the product defconfig and every SDK component linked (map file), each with
   its user (power manager, leak/humidity wake, K230 link, Wi-Fi bridge, BLE provisioning) or unused.
2. **SDK features off.** Shell and logging in the release build, unused peripherals, BLE beyond provisioning,
   lwIP options (the K230 owns IP: the BL616 is only the radio, DESIGN.md 5.5), Wi-Fi features.
3. **Compiler and linker.** `-Os` vs `-O2`, `-ffunction-sections -fdata-sections` + `--gc-sections`, LTO; what runs
   from TCM vs XIP flash (wake path in TCM for speed and power).
4. **Measure every change:** image size, wake-to-sleep awake time of a humidity slice (target: as short as possible,
   ~85 ms in the design), cold boot to console, current awake and asleep (test030), the functions the change
   touches (wake sources, K230 link, Wi-Fi join).

## Pass

Every enabled SDK feature has a named user; the awake time of a humidity slice and the image size are reduced or
justified; no wake source, link or Wi-Fi function regresses.

After the optimisation: the tests whose part changed run again (user, 2026-10-22), each with its full metrics, so
the gain and any regression show per part.

## Results

Not run yet.

### Baseline 2026-10-22

| Part | Size | Note |
|---|---|---|
| product firmware | 1.27 MB (2026-10-21 build) | FreeRTOS, Wi-Fi, BLE, lwIP; does not boot on rev 1 yet |
| test firmware | 121 KB | |
| boot2 | 53 KB | SDK boot2 (ISP release) |

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
| Image (sizes) | product 1.27 MB, BL616 test firmware 121 KB (+ boot2 53 KB) | test200 / test201 | the image this run used |
| Compile flags | BL616 -Os -g, -ffunction/-fdata-sections, --gc-sections, -flto (SDK) | test200 / test201 | |

## Boot time

The point of this test: boot time before / after each change, with its size.

## Battery energy

Smaller images mount and load faster: the energy per wake before / after each change (ENERGY.txt section 3).

## Obstacles

None yet.
