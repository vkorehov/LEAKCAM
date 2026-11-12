# test143: Take photo -> two real pictures at the server (product pair: the check-in's capture, SHOOT, the K230's upload)

Status: **pass** (2026-11-12 07:22 UTC, run.py 3 rounds warm and cold; BL616 full 0.6+g3c58c78-dirty,
K230 0.6+g3c58c78-dirty): both cameras' pictures at the server 6.3-9 s after the check-in (the K230's part 1.5 s:
cameras 0.65 s, two JPEGs 0.6 s, two uploads 0.2 s), capture time in X-Taken and EXIF, the board's own check-ins
unharmed afterwards. The leak's record (4 pictures, 2 per camera 2 s apart) went through the same path (4 uploaded).
Videos: not built yet (SENT counts them skipped).

The app's Take photo for a board out of BLE range (DESIGN.md 5.3.14): the server queues it, the board's next check-in
answer carries `"capture":{"kind":"photo","ids":[...],"cams":2}`, the BL616 (firmware/bl616_full/capture.c) sends
`SHOOT` to the K230, the K230 (firmware/k230_app/media.c) asks for the Wi-Fi bridge (`WIFI`), takes one lit colour
frame per camera, encodes each as a JPEG with the moment in EXIF (jpeg.c), uploads it to `POST /v1/media/<id>` with
`X-Taken`, and answers `SENT`. One picture per camera, each its own file (user 2026-11-12): p1 from J4 (camera 0), p2
from J5 (camera 1). The leak's record (4 pictures, 2 per camera 2 s apart) is the same path with two rounds.

## How it works

- Server (server/alert.go): `POST /v1/devices/<id>/capture {"kind":"photo"}` queues it; the next `POST /v1/poll`
  answer hands it over with the IDs `<dev>-<unix s>-p1`, `-p2`; `POST /v1/media/<id>` stores the file, `X-Taken`
  (unix ms) becomes the file's time, returned on `GET /v1/media/<id>`.
- BL616 full: `capture_answer()` after every check-in that reached the server: `SHOOT,photo,<dev>-<unix s>,2,2,0,0,
  <server>,8090` to the running K230 (or a session for it); the K230's `WIFI`: `wifi_link_start()` (NetHub over SDIO)
  in a task of its own; `SENT`: logged, the K230 off on battery; the bridge stops with the session's rails.
- K230: `WIFI` first (the join and DHCP run while the cameras shoot), the SDIO card bound (`/dev/bl616` rescan), the
  frames from the leak job's open cameras (one user at a time), JPEG quality 85 (software, plain C), each upload
  retried 3 times (a check-in takes the radio for a few s).

## Setup

Board on the bench on USB power (the K230 on), the full firmware, the server on the Pi; the cameras in place, the
LEDs light the shot. No hand, no phone.

## Steps

1. `run.py`: Take photo at the server, `presence now` on the BL616, SENT within 60 s; both IDs at the server: JPEG,
   1280x960, real light (mean brightness > 0), X-Taken and EXIF within 2 min of now and equal to each other within
   1 s; the K230's steps (bridge, capture, JPEG, upload) timed; BL616 and K230 memory before / after; repeated 3 times
   for leaks.

## Pass

Every round: SENT 2 uploaded 0 failed, both pictures valid with their time; no memory growth over the rounds.

## Result

- 2026-11-12, after the fixes below: warm 3/3 and 5/5 rounds, cold (the K230 off before each round, the photo starts
  its session) 3/3; every picture a 1280x960 JPEG of the real scene (mean 83-100), 33-57 KB, p1 and p2 the same
  X-Taken (one shot, both cameras), EXIF DateTimeOriginal = X-Taken in UTC; 5/5 check-ins after the photos reach the
  server; K230 program heap flat, BL616 free heap flat after the bridge's first start.
- Obstacles met on the way (each fixed, HISTORY.md): every second camera grab failed (the ISP cannot restart a
  stream: the streams now run while the cameras are open, vicap_cap.c); a second WIFI in a session reset the running
  bridge (capture.c leaves it alone); the K230 asked for /dev/bl616 before it existed after a cold boot (media.c
  retries by time); the BL616's own check-in lost frames while the K230's bridge shared the radio (3-8 s handshakes,
  2 of 7 failed: the bridge now lives only for the capture, the check-in waits for SENT); a capture queued behind
  another waited for the next interval (the answer's `more`, a check-in right after SENT).
- 2026-11-12 afternoon, native: 2592x1936 JPEGs from the VPU (123-183 KB), both cameras uploaded within ~1 s of
  SHOOT (the round 0.4 s; the software encoder it replaced took 1.4 s per picture, removed), warm 3/3 and cold 3/3
  with 5/5 check-ins after (the bridge's end now also leaves the station: before, a cold round left a stale
  association and the next check-ins failed, twice switching the board to the setup firmware).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: the K230 runs already (USB power); its boot is test045/test113's | test022 | |
| This part's boot share | n/a: the media thread waits for SHOOT, nothing at boot | - | |
| Test run time | 45-60 s automated for 3 rounds + 5 check-ins (run.py, 2026-11-12), no setup | - | |
| Photo: ask -> SENT | 6.3-9.0 s `presence now` -> SENT (warm and cold, 11 rounds), both pictures | user-facing <= 15 s after the check-in (memory leakcam-user-wait-15s) | BL616 log |
| K230 steps: bridge, capture, JPEG, upload | SHOOT -> bridge bound 0.15 s, both cameras 0.60-0.66 s (LEDs until AE steady), 2 JPEGs +0.6 s, 2 uploads +0.1-0.3 s: all at the server 1.4-1.7 s after SHOOT (warm); cold +0.8 s (camera open) | - | K230 console steps |
| Current idle / during | n/a: on USB power, not metered | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 156024 -> 156024 B (min 126380) over rounds 2-3 (the bridge's first start takes ~11 KB for good, round 1); K230 program heap +1056768-1196032 B, flat over 5 rounds (JPEG buffers reused by malloc): no leak | no leak | over the rounds |
| Image (sizes) | BL616 full 1232160 B; K230 program /app/leakcam 867328 B (MPP camera libraries + media/jpeg) | minimal image (rule) | |
| Speed / rate against the top | the person's wait is the check-in (60 s bench, 60 min product) + 6-9 s; the K230's own part 1.5 s of it: JPEG in software 0.3 s per picture (the VPU's JPEG would cut it), upload ~3 Mbit/s effective for 35-57 KB (connection setup dominates; test100: 27.5 Mbit/s) | SPEEDS.txt | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power, not metered | - | |
| Per day | n/a: a photo is asked by a person, not scheduled | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: a few s of capture and upload | K230 limit (datasheet) | |
| Compile flags | K230 program -Os -march=rv64imafdcv, sections + --gc-sections, strip; BL616 -Os -g | smallest code (rule) | |
| Features over the minimal image | capture.c (BL616 full); media.c, jpeg.c, http_post.c, the colour plane in vicap_cap.c (K230) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
