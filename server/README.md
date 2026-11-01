# LEAKCAM server (presence)

Use case 2 (tests/test111): each LEAKCAM checks in with its device ID (the BL616's factory MAC, 12 hex digits); the
setup app asks which of its LEAKCAMs are online. State is in memory: after a restart the devices check in again.

| Request | Answer |
|---|---|
| `GET /` | `"ok"` |
| `GET /v1/poll?id=<id>&ssid=<ssid>` (the board, every poll interval) | `{"poll":<s>,"stream":0}`; the board takes its next interval from `poll` |
| `GET /v1/devices/<id>` (the app) | `{"id":..,"online":true/false,"last_seen_s":<s or -1>,"ssid":..}` (the SSID it checked in from); offline after three missed intervals |

Runs on the Pi (vhome, 192.168.1.159:8090) as the systemd unit `leakcam-server`:

```
server/deploy.sh [poll seconds]     # build for aarch64, copy, (re)start; the interval (default 60) goes into ~/leakcam-server/env
```

Plain HTTP on the LAN; TLS and device identity come with the real server (test109, SECURITY.txt section 4).
