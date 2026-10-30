# test015: SDIO speed, K230 MMC0 <-> BL616 SDIO device

Status: **pass** (2026-10-21): 50 MHz 4-bit (the top), bus 21 MB/s, end to end 9.4 MB/s with the echo device, no CRC
errors; soak 56 runs / 109 MB clean.

The Wi-Fi link's bus alone, without Wi-Fi or NetHub: the BL616 runs the SDK's SDIO2 echo device (`sdio2_test`) as
`pt_sdu on` in `bl616_powertest` (4 ports x 2 KB download buffers; every block written comes back on the upload
ports). The K230 runs its NetHub driver with a temporary bench ([k230_bench.patch](k230_bench.patch), SDK copy only):
`bl616_scan` binds the card (the agent's rescan), `bl616_clk` shows or sets the clock, `bl616_bench <frames> [bytes]`
pushes messages through the driver's ring and ports and times them (rdtime, 27 MHz).

Order: BL616 `pt_off`, `pt_boot1 low`, `pt_on`, `pt_sdu on`, `pt_run`; K230 `bl616_scan`, `bl616_bench 2000`.

## Setup

**MISSING**; the setup is in the introduction above.

## Needs (build features and dependencies)

K230 MMC0 + bl616_nethub SDIO driver (bench patch); BL616 SDK sdio2 (PT_TEST_SDU, SDIO2_MAX_PORT_NUM=4).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

**MISSING**; the steps are the checks listed in Result (`bl616_bench 2000`, `bl616_bench <n> 512`, a ~10 min soak).

## Pass

**MISSING**; every Result line carries its pass / fail.

## Result

All 2026-10-21 (this test's only run):

- Attach: bound SDIO 424c:0606, io port 0x10000, 2048 B buffers, 4 ports: pass.
- Bus mode **50 MHz, 4-bit, high speed**: the SDIO core's top without UHS (the BL616's SDIO 2.0 device has none);
  the clock is already at its maximum, no tuning needed.
- 2000 x 1514 B (block mode, 3 x 512): **bus writes 21.4 MB/s, bus reads 21.5 MB/s, end to end 9.4 MB/s**; 200 x 1514 B
  the same (21.4 / 9.3 MB/s); 50 x 512 B (byte mode, 1 block): bus 20.5 MB/s, end to end 4.0 MB/s (per-message
  overhead): pass.
- End to end is limited by the echo device (it re-arms a download buffer only after its echo went up: BL616 busy 499
  times in 2000 messages); NetHub re-arms at once, so the product will sit between 9.4 and 21 MB/s.
- Errors, BL616 side: CRC 0, aborts 0. Soak ~10 min, 2000 x 1514 / 2000 x 512 B alternating: 56 runs, 0 stalls,
  0 resets; 112344 blocks in, 112343 out, 109 MB, CRC 0, aborts 0: pass.
- Against Wi-Fi: 9.4 MB/s (75 Mbit/s) end to end, 21 MB/s (170 Mbit/s) on the bus, well above the BL616's Wi-Fi
  (realistically 20-30 Mbit/s TCP): the SDIO link is not the bottleneck.
- Round-trip time not measured: the bare echo device raises no upload interrupt, the K230 driver collects echoes on
  its 1 s safety poll.

Open:
- The product BL616 firmware with NetHub did not reach its USB console on rev 1 at the time of this run, so this test
  used the SDK's `sdio2_test` echo device; the NetHub end-to-end rate (expected 9.4-21 MB/s) is not measured here.

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
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: SDIO card scan and attach time (bl616_scan: card bound) - done at WIFI time in a session, not at boot.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: BL616 + K230 current while transferring vs idle; energy per MB over SDIO (part of every upload).
