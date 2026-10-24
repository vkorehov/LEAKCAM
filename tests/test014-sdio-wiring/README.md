# test014: SDIO wiring, K230 MMC0 <-> BL616 SDIO, 2026-10-19

Status: **pass: CLK, CMD, D0-D3 both directions, no shorts** (2026-10-19).

The Wi-Fi link's six lines (BL616 IO13 CLK, IO12 CMD, IO14/15/10/11 D0-D3, 10 k pull-ups to the switched 3V3) without
the BL616's SDIO device firmware. BL616: `bl616_powertest` `pt_sdio`; K230: a temporary reader of its SD host's
present-state register (MMC0 at 0x91580000, offset 0x24: D0-D3 in bits 20-23, CMD in bit 24; via `/dev/mem`) and the
NetHub driver's probe (`/dev/bl616` ioctl 0x4c01).

## Setup

**MISSING**; the setup is in the introduction above and in the Results.

## Needs (build features and dependencies)

BL616 PT_TEST_SDIO; K230 MMC0 pins (no SDIO stack).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the rows of the Results tables.

## Pass

**MISSING**; every Results row carries its pass / fail.

## Results

| Step | Result | |
|---|---|---|
| Idle, nobody driving | CMD and D0-D3 high (present state 0x03ff00f0) | pass |
| BL616 drives CMD/D0-D3: all high, each one low, each one high, all low (12 patterns), K230 reads | every pattern read back exactly: each line connected, none stuck, no two shorted | pass |
| K230 probes MMC0 (rescan), BL616 counts edges for 8 s | CLK 4.78 M edges (undercounted at 2.1 M samples/s), CMD 126, D0-D3 0 (no data phase without a card) | pass |
| Probe result | "no card" after 35 ms: expected, the BL616 runs the test firmware, not its SDIO device | |

**Pass.** Next: the BL616 SDIO device firmware, enumeration, Wi-Fi, and the bus speed raised to the fastest clock
that runs without CRC errors.

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

## Metrics (**MISSING** on a re-run: skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | **MISSING** (2026-10-21); fill on a re-run | test022 | longest step: |
| This part's boot share | **MISSING** (2026-10-21); fill on a re-run | - | |
| Test run time | **MISSING** (2026-10-21); fill on a re-run | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | **MISSING** (2026-10-21); fill on a re-run | SPEEDS.txt | % of the top |
| Current floor / typical / peak | **MISSING** (2026-10-21); fill on a re-run | floor (test031) | at the probe above |
| Voltage under load (min) | **MISSING** (2026-10-21); fill on a re-run | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** (2026-10-21); fill on a re-run | - | scripts/energy.py |
| Per day | **MISSING** (2026-10-21); fill on a re-run | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** (2026-10-21); fill on a re-run | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | not recorded at the time (before the metrics rule) | minimal image (rule) | the image this run used |
| Compile flags | not recorded at the time (before the metrics rule) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

n/a: a wiring check.

## Battery energy

n/a: a wiring check.

## Obstacles

- **K230 MMC0 pads are dedicated, not GPIO** (no `pinRead`) -> the SD host's present-state register shows the live
  line levels.
- **The BL616 cannot count a 400 kHz-and-up clock exactly** by polling (2.1 M samples/s) -> edges only prove the line
  toggles; speed comes with the real link.
