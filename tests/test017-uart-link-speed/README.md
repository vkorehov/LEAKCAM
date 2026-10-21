# test017: K230 <-> BL616 UART link: speed, latency, errors

Status: **planned** (2026-10-21).

The control link (K230 UART1 <-> BL616 UART0, 115200 8N1, test009) carries READY/WAKE/SLEEP/WIFI/TIME frames.
Latency matters (a session waits on each ACK, 300 ms resend timeout); throughput only if it ever carries more.

## Steps

1. BL616 test firmware: an echo mode on UART0 (`pt_uart echo`); K230 temporary command sends frames and times the
   echo: round-trip latency (median, max) at 115200.
2. Bulk both ways: 64 KB with a checksum, errors counted, at 115200, 230400, 460800, 921600, 2 M baud (both ends
   switched together).
3. The highest baud with 0 errors over 10 MB, and the latency gained.

## Pass

Latency and error-free limit recorded; the product keeps 115200 unless a faster rate is clean with margin and buys
session time.
