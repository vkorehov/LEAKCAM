# LEAKCAM Wi-Fi: BL616 NetHub bridge and the K230 SDIO driver

The BL616 (U11, Ai-M62-CBS) is the K230's Wi-Fi. It runs Bouffalo's NetHub, which turns its
station interface into an Ethernet bridge on its SDIO device interface. K230 MMC0 is the SDIO
host. RT-Smart sees an ordinary Wi-Fi station (`wlan0`); its lwIP owns the IP address.

| Side | Code | What it does |
|---|---|---|
| BL616 | `firmware/bl616_wifi/wifi_link.c` | NetHub bootstrap, receive filter, Wi-Fi join from the stored credentials, control channel |
| BL616 | `firmware/bl616_wifi/main.c` | bring-up harness only: powers the K230 (`bl616_pwrmgr/k230_power.c`) and starts the link |
| both | `firmware/bl616_wifi/wifi_ctrl_proto.h` | the control messages, copied into the K230 driver by `install.sh` |
| K230 | `firmware/k230_board/rtsmart/drivers/bl616_nethub/` | SDIO transport, NetHub message router, RT-Thread WLAN device |

## Who does what

- **BL616:** radio, WPA (it keeps the EAPOL frames for its supplicant), association and reconnect.
  It joins with the credentials that BLE provisioning stored (`wifi_ssid` / `wifi_psk` in
  easyflash) as soon as its Wi-Fi is up, which is while the K230 is still booting. It has no IP
  address and runs no DHCP client (`use_dhcp = 0`).
- **K230:** DHCP, ARP, ICMP, DNS, NTP and every socket, with the BL616's station MAC.
- **Receive filter:** NetHub's built-in policy keeps DHCP and ICMP on the BL616 and makes it the
  IP owner. `wifi_link.c` replaces it: EAPOL stays on the BL616, every other frame goes to the
  K230.

## Protocol

### SDIO transport (SDU, function 1, id 424c:0606)

Both directions move whole messages through 4 ports, used in ring order by both ends.

| Register (CMD52, function 1) | Use |
|---|---|
| 0x02 HOST_INT_MASK | K230 writes 0x01: interrupt on uploads |
| 0x03 HOST_INT_STATUS | K230 writes 0 in its interrupt handler, which releases DAT1 |
| 0x04 RD_BITMAP | bit n: upload port n holds a message |
| 0x06 WR_BITMAP | bit n: download port n has a free BL616 buffer |
| 0x08 + 2n RD_LEN | packed length of upload n: bit 0 = two bytes, bit 1 = blocks, count = value >> 2 |
| 0x60 | K230 writes its upload buffer / 64 (32 = 2048 B): "host ready" |
| 0x61 | BL616 writes its download buffer / 64 (24 = 1536 B): "card ready" |
| 0x62 | status: bit 6 application running, bit 5 packed RD_LEN |
| 0x63 | next download port << 4, next upload port, read once at attach |
| 0x78-0x7a | CMD53 address of port 0; port n is at that address + n |

A message is one CMD53 to the port address with a fixed address (FIFO), in byte mode up to 512
bytes and in whole 512-byte blocks above. Card detection resets the SDU (CCCR I/O abort, RES), so
a K230 restart resynchronises both ends.

### Message router

Every message starts with `tag, sub_tag, len (LE16)`; `len` counts the bytes after these 4.
Both channels used here add `reserved, flag, credit_update, credit_limit`:

| Tag | Channel | Payload |
|---|---|---|
| 5 | netdev (station) | one Ethernet frame (up to 1514 bytes) |
| 13 | virtual channel | `type, reserved, len (LE16)` + data; type 1 (USER) carries the control messages below |

Handshake per channel: K230 `HOST_READY` (flag 4) -> BL616 `DEVICE_START` (flag 7, netdev payload
= station MAC + 4 bytes IPv4). Then `DNLD_DATA` (1) K230 -> BL616 and `UPLD_DATA` (2) BL616 -> K230.
`DEVICE_RESET` (6) or `DEVICE_STOP` (8) from the BL616 restart the handshake. The K230 resends
`HOST_READY` if `DEVICE_START` has not come within 1 s.

Download credit: when `credit_update` is set, `credit_limit` is the absolute number (mod 256) of
`DNLD_DATA` messages the BL616 can take; the K230 counts what it sent since `DEVICE_START` and
waits when it is level. netdev starts with 8, the virtual channel with 3.

The BL616's host keepalive (tag 4) is not used: its watchdog arms only after the first keepalive,
and a K230 restart already shows as the SDIO reset of card detection.

### Control messages (`wifi_ctrl_proto.h`)

| Type | Direction | Body |
|---|---|---|
| 0x01 GET_STATUS | K230 -> BL616 | none; sent every time the virtual channel starts |
| 0x02 JOIN | K230 -> BL616 | ssid_len, psk_len, ssid[32], psk[64]; for this power-up only, not stored |
| 0x03 LEAVE | K230 -> BL616 | none; disassociate, no reconnect until the next JOIN |
| 0x04 SCAN | K230 -> BL616 | none |
| 0x81 STATUS | BL616 -> K230 | state (0 down, 1 joining, 2 up, 3 failed), rssi, channel, bssid, ssid |
| 0x82 SCAN_ITEM | BL616 -> K230 | channel, rssi, auth, bssid, ssid; one per AP |
| 0x83 SCAN_DONE | BL616 -> K230 | number of items |

STATUS is also sent unsolicited on every link change, but only after the K230 has spoken since
the BL616 booted. The K230 driver turns STATUS into WLAN manager events: up = CONNECT (lwIP brings
the link up and starts DHCP), up -> anything = DISCONNECT, failed = CONNECT_FAIL. `wifi join`,
`wifi scan` and `wifi disconnect` in msh map to JOIN, SCAN and LEAVE.

## Build

```
cd ~/k230d-hw/LEAKCAM/firmware/bl616_wifi
make                                     # in the k230-rtos-sdk-build container on the arm64 host, see BUILD.md 4.1
make flash COMX=/dev/ttyACM0             # through PR1, as bl616_pwrmgr
```

The K230 side is built by the normal image build: `install.sh` copies the driver and the
protocol header into RT-Smart, and the LEAKCAM kernel config enables `RT_USING_BL616_NETHUB`.

## Bring-up order

The BL616 owns the K230's power, and the SDIO pads have their 10 k pull-ups on the K230's switched
3V3, so the order is fixed:

1. BL616 boots, `k230_power_init()` parks every K230-side pin.
2. `k230_power_on()`: rails up, reset released; the K230 boot ROM starts.
3. `wifi_link_start()`: RF, lwIP, easyflash, NetHub (SDU clock and pads, CARD_READY), then Wi-Fi.
   This has to finish before RT-Smart probes MMC0, about a second after reset.
4. RT-Smart: the board's SDIO Wi-Fi thread detects the card (3 tries), the driver binds 424c:0606,
   writes HOST_READY, starts both channels, registers `wlan0` with the MAC from `DEVICE_START`, and
   asks for STATUS.
5. BL616: `CODE_WIFI_ON_MGMR_DONE` -> join with the stored credentials -> CONNECTED -> STATUS up
   -> K230 DHCP.
6. Before `k230_power_off()`: `wifi_link_stop()` (SDU off, pads released); `k230_power_off()` parks
   them. A later `wifi_link_start()` re-arms only the SDU; the association is kept.

In the product `main.c` goes away: the power manager calls `wifi_link_start()` / `wifi_link_stop()`
around its K230 session. That needs the scheduler running during a session, which the battery path
of `bl616_pwrmgr/main.c` does not start yet.

## Check first on the board

1. **The card answers at all.** RT-Smart log: `SDIO Wi-Fi card detected on host 0`, then
   `bl616.sdio: BL616 SDU ready: io port 0x..., download 1536 B, ports 0/0`. Without it, check
   the BL616 console (USB CDC on PR1) for `sdio2 init done`, and whether step 3 was late.
2. **The function-1 CIS.** The install.sh patch in the RT-Thread SDIO core assumes the BL616 has no
   FUNCE tuple (so the core would refuse the function). If the log shows `function 1 CIS reports
   zero max block size`, the IDs are not 424c:0606 on this silicon: read them from the log line
   `no driver for SDIO function 1 ... id xxxx:yyyy`.
3. **Bus width and clock.** The host runs 4 bit, high speed (SDIO 2.0 allows 50 MHz). If CMD53
   fails with CRC errors, look at the signals before suspecting the protocol. There is no Kconfig
   knob for a slower clock or 1-bit mode: the ceiling is `SDHCI0_CARD_MAX_CLOCK` in
   `drivers/interdrv/sdio/drv_sdhci.c`, and 1 bit would need `MMCSD_BUSWIDTH_4` dropped there.
4. **The card interrupt.** `wifi scan` must answer within about a second. If replies only come
   once a second, DAT1 interrupts are not reaching the host and everything runs on the 1 s safety
   poll: check the 10 k pull-up on D1 and the host's card-interrupt enable.
5. **Handshake.** Both `bl616.msg: netdev: started, credit 8` and `vchan: started, credit 3`,
   then `bl616.wifi: station xx:..`. The BL616 console prints `HOST_READY` / `DEVICE_START` lines.
6. **Frames.** `ifconfig` shows `wlan0` with the BL616 MAC; after the link is up DHCP must give an
   address and `ping` the gateway must work. If DHCP never completes, the receive filter is not in
   place (BL616 log: `custom wifi rx filter is enabled`).
7. **Back-feed.** With the K230 off, 3V3 must stay at 0 V: the SDIO pads must be analog then.

## Not verified without hardware

- That the BL616 SDU function 1 CIS lacks FUNCE (taken from Bouffalo's Linux host driver, which sets
  the 512-byte block size and 200 ms enable timeout by hand for that reason).
- That `CODE_WIFI_ON_CONNECTED` comes after the WPA handshake with `use_dhcp = 0`, and that the
  station netif is up then (NetHub drops frames to a down netif).
- That the MAC in `DEVICE_START` is the real station MAC: NetHub reads it with
  `wifi_mgmr_sta_mac_get()` before Wi-Fi is initialised (as the SDK example does). The K230 fixes
  the netif MAC at registration and ignores `MAC_IP_UPDATE`; if `ifconfig` shows a different MAC
  than the BL616 console, no unicast frame will reach the K230.
- That the Wi-Fi manager's autoconnect retries after a failed first join and after a loss.
- That `wifi_mgmr_sta_connect()` while already associated switches networks cleanly.
- The 500 ms pause before `fhost_init()`: copied from the SDK example, reason not documented.
- Throughput and latency; nothing is tuned. Each message costs 2-4 CMD52 plus one CMD53.
