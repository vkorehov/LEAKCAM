# test018: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Why the 2026-10-30 runs at 2 Mbaud failed (BL616 state, not the rate)

The K230 -> BL616 direction lost 1-16 bytes per 64 KB / 1 MB echo (the BL616 echoed fewer than were sent; the
K230 received every echoed byte, 0 UART errors). The BL616 test firmware had then run all day with ~50 `pt_wifi
bridge` starts: heap 117.7-118.5 KB free and 147-150 blocks against 176.5 KB / 14 fresh. Freshly flashed (with a new
RX FIFO overflow counter in pt_status): 7 x 1 MB, 0 lost, 0 overflows, also with the bridge up and the K230
power-cycled under it. Open: reproduce the leak by repeated bridge starts. Harness: reset the BL616 between tests.

### Automated run 2026-10-30 (run.py, 382 s): **FAIL**

8 of 8 runs of uart_bench 1024 at 4 Mbaud: 6021895 errors, 69904-69904 B/s.


### Automated run 2026-10-25 (run.py, 97 s): pass

8 of 8 runs of uart_bench 1024 at 4 Mbaud: 0 errors, 344360-349858 B/s.

### 2026-10-22, on the single-rate images (4 Mbaud from boot, nothing switches it)

K230 image with the UART driver patch, BL616 test firmware 06:59:02 (`pt_uart echo` = LINK_BAUD): `uart_bench 1024`
reports "uart1 at 4000000 baud" without setting it. **10 x 1 MB: 0 bad bytes, 353.0-353.3 KB/s** (88 % of the
line); 16 B round trip median 53-56 us, max 66 us, 2000 of 2000 ok. Die 48.2 C before and after. Boot to app init
0.483 s (unchanged). Current during the soak not measured (asked, no reading).

### 2026-10-22, rate sweep

Status: **pass** (2026-10-22): clean up to 4 Mbaud (10 MB, 0 errors), 6 Mbaud fails; latency 1.48 ms -> 55 us.

Setup of the sweep: K230 bench image with `uart_bench <baud> [kb]`
([leakcam_bench.c](../test017-nand-raw-speed/leakcam_bench.c), SDK copy): UART1 at the rate, 200 frames of 16 B
timed to their echo, then a checked bulk transfer with <= 48 B in flight (the serial RX ring is 64 B). BL616
bl616_powertest `pt_uart echo <baud>`: a polling task returns every byte. Both ends switched per rate; leakcam_agent
had ended by itself (it owns UART1 otherwise). USB-C 1 VBUS meter.

Steps of the sweep:

1. BL616 test firmware: an echo mode on UART0 (`pt_uart echo`); K230 temporary command sends frames and times the
   echo: round-trip latency (median, max) at 115200.
2. Bulk both ways: 64 KB with a checksum, errors counted, at 115200, 230400, 460800, 921600, 2 M baud (both ends
   switched together).
3. The highest baud with 0 errors over 10 MB, and the latency gained.

| Baud | Latency 16 B round trip (median / max) | Throughput (64 KB checked) | of the line | Errors |
|---|---|---|---|---|
| 115 200 | 1 479 / 1 488 us | 11 515 B/s | 99.96 % | 0 |
| 230 400 | 744 / 748 us | 22 989 B/s | 99.8 % | 0 |
| 460 800 | 376 / 384 us | 45 926 B/s | 99.7 % | 0 |
| 921 600 | 208 / 209 us | 83 400 B/s | 90.5 % | 0 |
| 1 500 000 | 131 / 139 us | 135 681 B/s | 90.5 % | 0 |
| 2 000 000 | 100 / 101 us | 180 784 B/s | 90.4 % | 0 |
| 3 000 000 | 68 / 75 us | 271 338 B/s | 90.4 % | 0; **10 x 1 MB: 0 errors**, 271 KB/s |
| **4 000 000** | **55 / 70 us** | **350 979 B/s** | 87.7 % | 0; **10 x 1 MB: 0 errors**, 354 KB/s |
| 6 000 000 | 41 / 52 us (frames ok) | bulk fails: 6 600 bad bytes, timeout | - | **fail** |

Above 460800 the ~10 % gap is the bench's window protocol, not the line. **Highest clean rate 4 Mbaud** (1.5x below
the failure at 6 Mbaud); the link runs 115200 today: adopting 3-4 Mbaud cuts every K230 <-> BL616 message 20-27x
(WAKE, READY, Wi-Fi control). **Adopted 2026-10-22 (user: "move to top stable speeds")**: LINK_BAUD 4000000 in
bl616/k230_link.h, UART1 set in the K230 UART driver by k230_board/install.sh (RT-Smart serial has no termios, the
agent cannot set it). Code only; the product images are rebuilt with the firmware rewrite, then this test reruns on them.

Superseded metric values:

- UART link throughput, Top / budget column: "115200 now; 2 Mbaud top (both chips)" (before the 4 Mbaud adoption,
  2026-10-22)
- Boot time section: "Time from RT-Smart up to READY acknowledged at each baud (link handshake share of the boot)."
- Battery energy section: "Session time saved by a faster baud x the K230's session current = energy saved per wake
  (the link itself draws next to nothing)."

## Obstacles

- **uart_bench results never reached the console** (2026-10-25, automated run): since test039 the K230 console shares
  UART1 with the link, so while the BL616 echoes for the bench the result lines were echoed back into the K230 ->
  uart_bench buffers its lines and prints them 1.5 s after the run; the BL616 leaves the echo after 300 ms of silence
  (tests/test017-nand-raw-speed/leakcam_bench.c, bl616_powertest main.c).
- **The command could not be typed** -> the bridge stops while echoing -> `pt_uart echo <command>` sends it over the
  link itself; the K230 shell's echo of that line came back and looped as input -> dropped for the first 5 ms.
- **The idle-priority echo task managed ~5 KB/s** (the Wi-Fi tasks of the test firmware take the CPU; the 32-byte FIFO
  overflows in 80 us at 4 Mbaud) -> the echo moved into the UART RX interrupt: 347 KB/s, round trip 70 us (55 us
  polled on 2026-10-22, the ISR's cost).
- **leakcam_agent's READY frames corrupted the bench stream** (40 of 200 frames bad, bulk stalled) -> the runner stops
  the agent first (on 2026-10-22 it had ended by itself).

(Since 2026-10-23 the K230 console is the BL616's `/dev/ttyACM1`, over the link UART from the first SPL line,
up across cold boots (tests/test039): the CH340 problems below are history, kept as the record of these runs.)

- **The K230 baud rate did not change, first run** (every rate gave exactly 11 515 B/s, the BL616 had switched) ->
  the serial layer refuses RT_DEVICE_CTRL_CONFIG with RT_EBUSY when the new bufsz differs from the open device's
  (RT_SERIAL_CONFIG_DEFAULT 64 vs UART1's 16384), and the return value was not checked -> copy the device's
  config, change only the baud, check and print the result.
- **The serial RX ring is 64 B** (RT_SERIAL_RB_BUFSZ) -> the bulk window was cut to 48 B.
- **The rate fell back to 115200 after the first run on the 4 Mbaud image** (2026-10-22 09:51, my own bench bug):
  uart_bench restored 115200 on exit ("back to the link's rate for the agent", from before the change); 9 queued runs
  then timed out at 15 + 20 s each (~6 min) -> **one rate only** (user: "keep single speed", "don't like switching
  speeds"): uart_bench takes no rate and never sets one, it reports what the UART driver set at boot; BL616 `pt_uart`
  always runs LINK_BAUD (firmware/bl616/k230_link.h, included by the test firmware too). The sweep above stays as
  the record of how 4 Mbaud was chosen.
- **Soak read nothing for 2 min** (10:01-10:04, my mistake): the CH340 re-enumerated after the NAND boot as
  root:dialout and my script skipped setfacl -> permanent fix: udev rule tools/power-bringup/99-leakcam.rules (all four
  board USB devices 0666 on every plug; user: "remember already permissions").
- **Queued msh commands** (my mistake): commands sent every 3.6 s while each run took 35 s queued up in msh ->
  send the next only after the previous result line.
