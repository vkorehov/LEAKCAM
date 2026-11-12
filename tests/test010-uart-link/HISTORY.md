# test010: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 244 s): pass

8 boots: 112 READY frames with valid checksums on the BL616, echo runs clean.


### Automated run 2026-10-31 (run.py, 240 s): pass

8 boots: 112 READY frames with valid checksums on the BL616, echo runs clean.


### BL616 off USB, 2026-10-30 20:55 (open, not reproduced)

After echo runs with the Wi-Fi bridge (K230 power-cycled under it) the BL616's USB left the bus (`usb 2-4.1: USB
disconnect` 20:55:01, no VM freeze then: the last ended 20:45) and came back only with the cable replugged (07:58:40
next day). The BL616 itself never reset: uptime 40240 s since the 20:49:47 flash, its counters kept. Its Wi-Fi had
gone deaf: `pt_trace counts` 2 connects, then 5052 disconnect/retry pairs every ~8 s from ~160 s after boot, all
night; a scan found 0 APs (also after `pt_wifi leave`) while the router was up; after `reboot` 27 APs. Not reproduced
(2026-10-31): the K230 switched off under a joined bridge (joined, 25-28 APs for 90 s), and the exact replay of the
evening's two scripts (joined, 23-25 APs). Note: `pt_off` only stops the bridge's SDIO (`wifi_link_stop`, the
product's k230_off() too): the radio stays associated with autoconnect on. Since 2026-10-31 run_tests.py logs the
BL616's heap, Wi-Fi state and trace counts after each test and reboots it, so a test never inherits such a state.

### Bench stream corrupted by other UART1 writers, not the link: 2026-10-31

Correction of the 2026-10-30 note below ("BL616 state"): a fresh BL616 has the same heap after its first bridge
start (118 KB free, 122 blocks), so the heap was not the cause; and the "with the bridge up" echoes of 2026-10-30 never
ran (`pt_uart echo` answered "3V3 is off": its 3V3 check reads the SDIO pull-ups, which the bridge drives).

Not the VM either: VMware's guest log records every VM freeze (30 s granularity), none overlapped a failing run
(test010 20:07-20:12, test018 20:12-20:18, test039 15:14), and the echo runs K230 -> BL616 -> K230 on the board.

Found with the bridge bound (2026-10-31, test firmware fixed to start the echo then): 1 run of 5 and 1 of 10 stalled
(944195 / 978501 of 1048576 back, 78-81 bad, 15 s timeout), always the run ~25 s after Wi-Fi came up. Byte counts
agree on both chips (BL616 echoed = K230 got back; K230 uart1: 0 framing/parity/overrun, 0 dropped; BL616 RX FIFO: 0
overflows, at most 24 of 32 full): no byte was lost on the line. The K230 printed `[I/ntp] Get local time ...` (66
bytes) into UART1 in the middle of the bench: the console shares UART1, the BL616 echoed the line, the bench took it
as data and its window never closed. Any console or link output during a bench does the same (yesterday's runs had
no bridge; the likely writers are the agent's READY frames / hook messages - not provable now, their console logs
were overwritten).

Fix (link_mux.c): while `uart_bench` holds uart1 (`link_mux_pause`), console output is held (1 KB) and written after
the bench, link frames are dropped (the sender repeats them); `link_stat` counts both. With it: 10 x 1 MB with the
bridge bound, 0 lost, the NTP line held (66 B) and printed after run 2.

### 2026-10-30 note (superseded above): why the runs at 2 Mbaud failed

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
