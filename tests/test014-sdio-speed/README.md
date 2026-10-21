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
| 2000 x 512 B | after ~13 (run 1) and ~360 (run 2) messages the K230 lost the card, then rebooted; BL616 no errors | **fail** |
| Errors, BL616 side | CRC 0, aborts 0 over 2500 blocks (3.4 MB) | pass |

- **End to end vs bus:** the echo device re-arms a download buffer only after its echo went up, so the K230 waits for a
  free port (BL616 busy 499 times in 2000 messages). NetHub re-arms at once; the product will sit between 9.4 and
  21 MB/s.
- **Round trip:** the echoes came back ~1 s after the start whatever the size: the bare echo device raises no upload
  interrupt, so the K230 driver collects them on its 1 s safety poll. Not a bus figure.
- **Against Wi-Fi:** 9.4 MB/s (75 Mbit/s) end to end, 21 MB/s (170 Mbit/s) on the bus, well above the BL616's Wi-Fi
  (realistically 20-30 Mbit/s TCP). The SDIO link is not the bottleneck; the clock is already at its maximum, no
  tuning needed.

**Pass** for the bus (50 MHz, 4-bit, 21 MB/s, no CRC errors). **Open:** exactly-512-byte messages crash the K230 side.

## Obstacles

- **Product BL616 firmware does not reach its USB console on rev 1**, even reduced to `board_init()` + scheduler (a
  boot probe build): the product configuration hangs before USB starts -> NetHub not usable yet; synthetic test with
  the SDK's `sdio2_test` added to the working test firmware (`pt_sdu`). Open: add the product's parts to the test
  image step by step.
- **512-byte messages** -> one block written in byte mode (count field 0 = 512) right at the byte/block boundary
  (`xfer_len`) -> open: reproduce with the clock lowered, try writing 512 as one block in block mode.
- **Port count:** `bflb_sdio2` defaults to 16 ports, the K230 driver and NetHub use 4 -> `-D SDIO2_MAX_PORT_NUM=4`
  in the test firmware, as `sdio2_test` and NetHub do.
- **Nothing scans MMC0 at boot** (only the agent, after `WIFI` on the link) -> temporary `bl616_scan`.
- **The echo device does not answer NetHub's HOST_READY** (it mirrors it) -> the bench opens the netdev channel
  itself and keeps its credits topped up ("raw" mode).
- **Echoes only on the 1 s safety poll** -> round-trip time not usable; the read rate is timed per CMD53 instead.
- **CH340 permissions** reset on every re-enumeration -> `setfacl` again after each K230 flash.
- **NAND now holds the test image** (bench, test hook in `/sdcard/app/leakcam_wake`) -> `install.sh` and the product
  image flash restore it.
