# test018: Wi-Fi throughput, K230 -> SDIO -> BL616 -> AP

Status: **planned** (2026-10-21).

The product path for clips and checks: the K230's lwIP over the NetHub SDIO bridge (test014: bus 21 MB/s, not the
limit) to the BL616's 2.4 GHz radio. Needs the product BL616 firmware (open on rev 1) and stored credentials
(`wifi_set` on the debug console, or BLE).

## Steps

1. Join, DHCP, DNS, NTP (time to each step from the WIFI command).
2. iperf (or a temporary TCP/UDP sender on the K230 and a receiver on the LAN PC): TCP up/down, UDP up with loss, at
   1 m and at the install distance; RSSI from the BL616.
3. The real load: two RTMP streams (H.265 2 x ~500 kbit/s + Opus) to MediaMTX: frames dropped, latency.
4. The SDIO bridge's own rate during it (NetHub credits, BL616 busy count).

## Pass

TCP up >= 2 x the two streams' rate with margin at the install distance; no RTMP drops; figures recorded.
