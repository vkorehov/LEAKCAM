# test015: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 101 s): pass

SDIO bus, K230 -> BL616 echo device: bl616_clk: 50000000 Hz, 4-bit, high speed 1; 2000 x 1514 B: bus 21246 KB/s, end to end 9084 KB/s; 200 x 1514 B: bus 21247 KB/s, end to end 9146 KB/s; 50 x 512 B: bus 20366 KB/s, end to end 3917 KB/s; soak: 32 of 32 runs clean

- BL616 echo device over the run: 66321 blocks in, 66321 out, 67108 KB; CRC errors 0, aborts 0, other 0, resets 0
- bl616_clk: 50000000 Hz, 4-bit, high speed 1
- 2000 x 1514 B: bus 21246 KB/s, end to end 9084 KB/s
- 200 x 1514 B: bus 21247 KB/s, end to end 9146 KB/s
- 50 x 512 B: bus 20366 KB/s, end to end 3917 KB/s
- soak: 32 of 32 runs clean


### Automated run 2026-10-31 (run.py, 134 s): pass

SDIO bus, K230 -> BL616 echo device: msh />; 2000 x 1514 B: bus 21226 KB/s, end to end 9076 KB/s; 200 x 1514 B: bus 21211 KB/s, end to end 9156 KB/s; 50 x 512 B: bus 20333 KB/s, end to end 3816 KB/s; soak: 32 of 32 runs clean

- meter K230 pushing SDIO frames to the BL616 echo device (bl616_bench 2000 back to back): 0.230 A
- BL616 echo device over the run: 244351 blocks in, 244351 out, 331675 KB; CRC errors 0, aborts 0, other 0, resets 0
- msh />
- 2000 x 1514 B: bus 21226 KB/s, end to end 9076 KB/s
- 200 x 1514 B: bus 21211 KB/s, end to end 9156 KB/s
- 50 x 512 B: bus 20333 KB/s, end to end 3816 KB/s
- soak: 32 of 32 runs clean


### 2026-10-21 (the run's full Results)

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

### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Superseded status line

Status: **pass: 50 MHz 4-bit, bus 21 MB/s, end to end 9.4 MB/s, no CRC errors; soak 56 runs / 109 MB clean** (2026-10-21).

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
- **CH340 permissions** reset on every re-enumeration -> `setfacl` again after each K230 flash. (Gone: udev rule
  99-leakcam.rules, and since test039 the K230 console is /dev/ttyACM1.)
- **NAND now holds the test image** (bench, test hook in `/sdcard/app/leakcam_wake`) -> `install.sh` and the product
  image flash restore it.

## Re-run for the metrics: 2026-10-31

- The bench moved into the shared test image (`build_test_image.sh` applies [k230_bench.patch](k230_bench.patch) to the
  SDK copy of the driver); rebased onto today's driver: `bl616_scan` dropped (the driver's `bl616_rescan` does the
  same), `leakcam_wake.c` hook dropped (the test image boots to msh).
- **First run stalled at frame 113, credit = sent = 49:** today's `dispatch()` takes `credit_update` / `credit_limit`
  from every upload, and the echo device mirrors the K230's own download headers back, so the echoes overwrote the
  bench's credit. The 2026-10-21 bench dropped echoes; the rebase lost it. Fixed: `bench_raw` makes `dispatch()` drop
  every upload while the bench drives the bare echo device. The meter reading of that run was not taken (no load).
