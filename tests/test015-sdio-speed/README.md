# test015: SDIO speed, K230 MMC0 <-> BL616 SDIO device

Status: **pass** (2026-10-31, run.py): 50 MHz 4-bit (the top), bus 21.2 MB/s, end to end 9.1 MB/s with the echo
device, 32 soak runs / 324 MB with 0 CRC errors, aborts or resets; 230-233 mA while busy (+10-13 mA over msh idle).

The Wi-Fi link's bus alone, without Wi-Fi or NetHub: the BL616 runs the SDK's SDIO2 echo device (`sdio2_test`) as
`pt_sdu on` in `bl616_powertest` (4 ports x 2 KB download buffers; every block written comes back on the upload
ports). The K230 runs its NetHub driver with a temporary bench ([k230_bench.patch](k230_bench.patch), SDK copy only):
`bl616_rescan` binds the card (the agent's rescan), `bl616_clk` shows or sets the clock, `bl616_bench <frames> [bytes]`
pushes messages through the driver's ring and ports and times them (rdtime, 27 MHz).

Order: BL616 `pt_off`, `pt_boot1 low`, `pt_on`, `pt_sdu on`, `pt_run`; K230 `bl616_rescan`, `bl616_bench 2000`.

## Setup

USB-C 1 meter (5 V VBUS), cell full (62 mA BL616-only before the session); BL616 test firmware (`pt_sdu`); the K230
test image (`build_test_image.sh` applies k230_bench.patch to the SDK copy of the driver, flashed by test019).

## Needs (build features and dependencies)

K230 MMC0 + bl616_nethub SDIO driver (bench patch); BL616 SDK sdio2 (PT_TEST_SDU, SDIO2_MAX_PORT_NUM=4).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

`run.py`: boot with `pt_sdu on` (rails up, K230 in reset), `bl616_rescan`, `bl616_clk`; `bl616_bench 2000 1514`,
`200 1514`, `50 512`; the meter reading with the bench back to back; 16 x (`2000 1514` + `2000 512`) with memory
samples (leak verdict); `pt_sdu` counters before and after.

## Pass

Every bench run complete (no STALL, no error stop), 0 CRC errors, aborts and resets on the BL616, no K230 reset, no
leak.

## Result

2026-10-31 (run.py, the test image):

- 2000 x 1514 B: bus 21.2 MB/s, end to end 9.1 MB/s; 200 x 1514 B: 21.2 / 9.2 MB/s; 50 x 512 B: 20.3 / 3.8 MB/s;
  BL616 busy ~500 of 2000 (the echo device re-arms late, as on 2026-10-21): pass.
- Soak 32 runs: 244351 blocks in and out, 324 MB, CRC 0, aborts 0, resets 0; no K230 reset: pass.

2026-10-21 (first run):

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

- The bus rate here is the SDK's `sdio2_test` echo device; the NetHub bridge end to end is test100's: TCP up
  27.5 / down 22.1 Mbit/s (2026-10-25).

## Metrics (skill leakcam-test-metrics, every value at its maximum)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value (typical / max) | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> app init (median / max of runs) | pt_run -> msh 0.52 / 0.52 s (median / max of 1; SPL 0.16, OpenSBI 0.27, RT-Smart 0.28, mounted 0.51, msh 0.52); longest step to mounted 0.23 s (2026-10-31, run.py) | test022 | longest step: |
| This part's boot share | n/a: the card binds after the boot (bl616_rescan, the agent's WIFI step) (2026-10-31) | - | |
| Test run time | 134 s automated (run.py, 2026-10-31), no setup | - | setup (flashing, plugging, waiting) separately |
| Speed / rate against the top | bus 21226 KB/s, end to end 9076 KB/s (2000 x 1514 B; the echo device limits end to end), 50 MHz 4-bit: the SDIO 2.0 top, no UHS on the BL616 (2026-10-31) | SPEEDS.txt | % of the top |
| Current floor / typical / peak | idle 0.220 (test006) / bl616_bench back to back 0.230-0.233 average / 0.237 peak A at 5 V (USB-C 1 VBUS meter, the user, 2026-10-31) | floor (test031) | at the probe above |
| Voltage under load (min) | VBAT 4.33 V min (K230 ADC ch 1, test023 calibration, 17 samples; on USB: the charger rail, not the cell) (2026-10-31) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | 233 mA x 4.33 V = 1009 mW; per 2000 x 1514 B (3 MB) (0.33 s) 0.33 J, 21 uAh; over idle (220 mA) 0.02 J, ~6 mJ per MB (2026-10-31) | - | scripts/energy.py |
| Per day | n/a: the bus rate test; the product's Wi-Fi traffic is test100 / ENERGY.txt (2026-10-31) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 37.3 / 45.5 / 45.5 C; board 31.0 / 34.7 / 34.7 C (AHT20) (2026-10-31) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 176464 / min 172252 / 176464 B, blocks 14 -> 14, deleted 0; K230 heap available 7025152 / 7025152 / 7270656 B; BL616 no leak (mem_leak.py, 16 snapshots); K230 no leak (mem_leak.py, 16 snapshots) (2026-10-31) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | K230 kdimg 45445120 B, BL616 test firmware 1313712 B (2026-10-31, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-10-31) | smallest code (rule) | |
| Features over the minimal image | n/a: the bench is test code (k230_bench.patch on the driver, test image only); the driver itself is the product's bl616_nethub (2026-10-31) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: SDIO card scan and attach time (bl616_scan: card bound) - done at WIFI time in a session, not at boot.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: BL616 + K230 current while transferring vs idle; energy per MB over SDIO (part of every upload).
