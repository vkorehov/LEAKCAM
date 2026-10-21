# test014: SDIO speed, K230 MMC0 <-> BL616 SDIO device, 2026-10-21

The Wi-Fi link's bus alone, without Wi-Fi or NetHub: the BL616 runs the SDK's SDIO2 echo device (`sdio2_test`) as
`pt_sdu on` in `bl616_powertest` (4 ports x 2 KB download buffers; every block written comes back on the upload
ports). The K230 runs its NetHub driver with a temporary bench ([k230_bench.patch](k230_bench.patch), SDK copy only):
`bl616_scan` binds the card (the agent's rescan), `bl616_clk` shows or sets the clock, `bl616_bench <frames> [bytes]`
pushes messages through the driver's ring and ports and times them (rdtime, 27 MHz).

Order: BL616 `pt_off`, `pt_boot1 low`, `pt_on`, `pt_sdu on`, `pt_run`; K230 `bl616_scan`, `bl616_bench 2000`.

## Results

| Step | Result | |
|---|---|---|
| Attach | bound SDIO 424c:0606, io port 0x10000, 2048 B buffers, 4 ports | pass |
| Bus mode | **50 MHz, 4-bit, high speed**: the SDIO core's top without UHS (the BL616's SDIO 2.0 device has none) | max |
| 2000 x 1514 B (block mode, 3 x 512) | **bus writes 21.4 MB/s** (3000 KB in 140 ms); **end to end 9.4 MB/s** (316 ms) | pass |
| Same run, uploads (echoes) | **bus reads 21.5 MB/s**; 3000 KB back of 3000 KB | pass |
| 200 x 1514 B | bus 21.4 MB/s, end to end 9.3 MB/s | pass |
| 50 x 512 B (byte mode, 1 block) | bus 20.5 MB/s, end to end 4.0 MB/s (per-message overhead) | pass |
| 2000 x 512 B | run 1 the K230 lost the card, run 2 and a soak run it reset; a later run clean: the resets are the watchdog (Obstacles), the stall behind them is being traced | open |
| Errors, BL616 side | CRC 0, aborts 0 over 2500 blocks (3.4 MB) | pass |
| Soak, ~10 min, 2000 x 1514 / 2000 x 512 B alternating (bench with the watchdog-safe waits, echoes dropped) | 56 runs, 0 stalls, 0 resets; BL616 112344 blocks in, 112343 out, 109 MB, CRC 0, aborts 0 | pass |

- **End to end vs bus:** the echo device re-arms a download buffer only after its echo went up, so the K230 waits for a
  free port (BL616 busy 499 times in 2000 messages). NetHub re-arms at once; the product will sit between 9.4 and
  21 MB/s.
- **Round trip:** the echoes came back ~1 s after the start whatever the size: the bare echo device raises no upload
  interrupt, so the K230 driver collects them on its 1 s safety poll. Not a bus figure.
- **Against Wi-Fi:** 9.4 MB/s (75 Mbit/s) end to end, 21 MB/s (170 Mbit/s) on the bus, well above the BL616's Wi-Fi
  (realistically 20-30 Mbit/s TCP). The SDIO link is not the bottleneck; the clock is already at its maximum, no
  tuning needed.

**Pass** for the bus (50 MHz, 4-bit, 21 MB/s, no CRC errors). The stalls were the echo device mirroring
NetHub's credit header back (the bench now drops echoes), the resets the watchdog starved by the bench's busy wait:
both in the test code, not the bus.



## Boot time

Not recorded (before the boot rule, 2026-10-21). If re-run: SDIO card scan and attach time (bl616_scan: card bound) - done at WIFI time in a session, not at boot.

## Battery energy

Not recorded (the test ran before the energy rule, 2026-10-21). If re-run: BL616 + K230 current while transferring vs idle; energy per MB over SDIO (part of every upload).

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

- **Product BL616 firmware does not reach its USB console on rev 1**, even reduced to `board_init()` + scheduler (a
  boot probe build): the product configuration hangs before USB starts -> NetHub not usable yet; synthetic test with
  the SDK's `sdio2_test` added to the working test firmware (`pt_sdu`). Open: add the product's parts to the test
  image step by step.
- **K230 resets silently during a bench** (no trap dump: U-Boot SPL is the next line; 3 times, with 512 and 1514 B
  frames, once on the first run) -> the 20 s hardware watchdog: `drv_wdt.c` starts it at boot, its feeder thread
  (priority 25) feeds every 10 s; the bench waited for a stalled transfer with `rt_thread_yield()`, which only lets
  equal-priority threads run, so the shell starved the feeder and the reset came ~20 s after the stall. Not the
  message size. -> bench waits with `rt_thread_mdelay(1)` and gives up after 3 s with a dump of the port state
  (`bl616_dump`); the soak then shows what stalls (see Results when it ran). Rule for the product driver too: no
  wait may spin without sleeping (skill k230-rtsmart-hangs).
- **Port count:** `bflb_sdio2` defaults to 16 ports, the K230 driver and NetHub use 4 -> `-D SDIO2_MAX_PORT_NUM=4`
  in the test firmware, as `sdio2_test` and NetHub do.
- **Nothing scans MMC0 at boot** (only the agent, after `WIFI` on the link) -> temporary `bl616_scan`.
- **The echo device does not answer NetHub's HOST_READY** (it mirrors it) -> the bench opens the netdev channel
  itself and keeps its credits topped up ("raw" mode).
- **Echoes only on the 1 s safety poll** -> round-trip time not usable; the read rate is timed per CMD53 instead.
- **CH340 permissions** reset on every re-enumeration -> `setfacl` again after each K230 flash.
- **NAND now holds the test image** (bench, test hook in `/sdcard/app/leakcam_wake`) -> `install.sh` and the product
  image flash restore it.
