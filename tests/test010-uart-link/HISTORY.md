# test010: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### BL616 off USB, 2026-10-30 20:55 (open)

After an echo test that ended with the Wi-Fi bridge up and the K230 switched off, the BL616 left USB (kernel: `usb
2-4.1: USB disconnect` 20:55:01) and did not come back, not even as the ROM; no VM hub errors, so not the VM. Its
watchdog did not bring it back. The rerun batch started at 20:57 found no port (test010 crashed in boot()) and was
stopped. Next: replug PR1, read `pt_trace` breadcrumbs and the reset reason; make the harness end the bridge after
every test. The morning's disappearance also followed bridge activity.

### Why the 2026-10-30 runs at 2 Mbaud failed (BL616 state, not the rate)

The K230 -> BL616 direction lost 1-16 bytes per 64 KB / 1 MB echo (the BL616 echoed fewer than were sent; the
K230 received every echoed byte, 0 UART errors). The BL616 test firmware had then run all day with ~50 `pt_wifi
bridge` starts: heap 117.7-118.5 KB free and 147-150 blocks against 176.5 KB / 14 fresh. Freshly flashed (with a new
RX FIFO overflow counter in pt_status): 7 x 1 MB, 0 lost, 0 overflows, also with the bridge up and the K230
power-cycled under it. Open: reproduce the leak by repeated bridge starts. Harness: reset the BL616 between tests.

### Automated run 2026-10-30 (run.py, 285 s): **FAIL**

8 boots: 112 READY frames with valid checksums on the BL616, echo runs clean; boot 1: echo run not clean; boot 4: echo run not clean; boot 8: echo run not clean


### Automated run 2026-10-25 (run.py, 243 s): pass

8 boots: 112 READY frames with valid checksums on the BL616, echo runs clean.

### First run 2026-10-18 (115200 8N1): pass

| Direction | How | Result | |
|---|---|---|---|
| K230 -> BL616 | BL616 `pt_uart on`, K230 reset: the agent sends `READY` | `$1,READY*FC` .. `$13,READY*0F`, checksums intact; each frame 5x (resent while no ACK comes) | pass |
| BL616 -> K230 | BL616 sends a line per second; K230 `cat /dev/uart1` | `LEAKCAM BL616 uart test 27` .. `40`, consecutive, none lost | pass |

**Pass.**

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Former placeholders (2026-10-18 README)

- Setup: **MISSING**; the setup is in the introduction above and in the Results.
- Steps: **MISSING**; the steps are the rows of the Results tables.
- Pass: **MISSING**; every Results row carries its pass / fail.

## Obstacles

- **Each READY 5 times** -> by design: the agent resends while no ACK comes (the test firmware sends none).
- **`cat /dev/uart1` hangs the shell (no Ctrl-C)** -> reset the K230 from the BL616 (`pt_hold`, `pt_run`).
- **`killall leakcam_agent` did not stop it** -> still listed afterwards; it did not get in the way.
