# LEAKCAM server (presence)

Use case 2 (tests/test111): each LEAKCAM checks in with its device ID (the BL616's factory MAC, 12 hex digits); the
setup app asks which of its LEAKCAMs are online. State is in memory: after a restart the devices check in again.

Every answer carries the server's time as the header `X-Time: <unix s>`: the boards set their wall clock from whatever
exchange came last, no NTP (DESIGN.md 5.3.12).

| Request | Answer |
|---|---|
| `GET /` | `"ok"` |
| `POST /v1/poll?id=<id>`, form `ssid=<ssid>&full=<version>&bat=..&chg=..` + the metrics (the board, every poll interval; DESIGN.md 5.3.10) | `{"checkin":<s>,"leakcheck":<s>,"stream":0,"ble":0/1}`; the metrics kept for `GET /v1/devices/<id>` (`metrics`) and appended to LEAKCAM_METRICS; the board takes its check-in and leak check intervals from `checkin` / `leakcheck` (per device: the PUT below); `ble` 1: a phone asked to wake it |
| `PUT /v1/devices/<id>/intervals` `{"checkin":<s>,"leakcheck":<s>}` (0 = the default: LEAKCAM_CHECKIN_S 3600, LEAKCAM_LEAKCHECK_S 86400) | the device's intervals now; its next check-in's answer carries them (DESIGN.md 5.3.12); kept in memory |
| `GET /v1/devices/<id>` (the app) | `{"id":..,"online":true/false,"last_seen_s":<s or -1>,"ssid":..,"wake":..}` (the SSID it checked in from; a wake waiting); offline after three missed intervals |
| `GET /v1/devices/<id>/pin` (the app, right before pairing) | `{"pin":"<6 digits>"}`: HMAC of the ID under the key in `LEAKCAM_PIN_KEY` |
| `POST /v1/devices/<id>/wake` (the app, a present board out of BLE range) | `{"within_s":<checkin>}`; the next check-in answers `"ble":1`, the board advertises for 2 minutes (tests/test119); 429 within 2 minutes of the last wake |

Runs on the Pi (vhome, 192.168.1.159:8090) as the systemd unit `leakcam-server`:

```
server/deploy.sh [check-in seconds] # build for aarch64, copy, (re)start; the interval (default 60 for the bench) goes into ~/leakcam-server/env
```

Plain HTTP on the LAN; TLS and device identity come with the real server (test109, SECURITY.txt section 4).
