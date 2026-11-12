# test109: secure server (TLS, device identity, pinned certificate, signed change net)

Status: **BL616 mTLS built 2026-11-10, not run on the board yet** (DESIGN.md 5.3.12 "Built"): one CA for the server and
every device (tools/pki/leakcam_pki.py), the credentials in the board's flash (provision_tls.py), tls_link.c (TLS 1.3 +
1.2 on mbedTLS), the server's :8443 mTLS listener on the Pi (server/mtls.go, `go test` TestDevicesOverMTLS). The K230's
TLS and the app's are still to do. QUIC was measured against it and dropped (DESIGN.md, 2026-11-10).

Today every request is plain HTTP / RTMP without authentication (SECURITY.txt 4). Needed before real use: TLS for
the check-in (`POST /v1/poll`), the per-device change net (`GET /v1/devices/{id}/model`), the crash records, the OTA
images and the clips (RTMPS to MediaMTX); a per-device identity so the server knows who
posts; the server's certificate pinned on the device; the change net checked against a signed value. This test
proves each and measures what TLS costs on a battery device.

## Setup

- The server on the Windows PC (192.168.1.131): HTTPS for the API and MediaMTX with RTMPS and publish
  authentication; a test CA, the server certificate, device credentials; switches for a wrong / expired /
  other-host certificate, a revoked device and a replayed request.
- The board on battery for the energy steps, on USB for the rest; the K230 console and the BL616 console.

## Needs (build features and dependencies)

K230: TLS client for its requests and RTMPS (library to choose: mbedTLS in the SDK, or what RT-Smart offers),
the device key storage, the time from the server's `X-Time` (no NTP; the BL616's `;t=`) and the pinned server key; BL616: TLS for `/v1/poll` **if it fits** - Wi-Fi RAM is
at 99.6 % (skill leakcam-product-build 7): step 1 decides whether the BL616's own check can use TLS at all.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Fit: TLS on the BL616 (heap and static RAM for one handshake next to Wi-Fi and BLE) and on the K230 (RAM, image
   size). If the BL616 cannot, the decision is written down (the check-in over TLS through the K230, a signed plain
   answer, or no BL616 check-in).
2. Handshake cost: full handshake and session resumption per request - time, bytes on air, energy (it repeats on every
   wake: resumption or a kept ticket matters).
3. Pinned certificate: a wrong CA, an expired certificate, another host name, a downgrade to plain HTTP: the device
   refuses each, sends nothing of the owner's, backs off.
4. Device identity: the server refuses an unknown and a revoked device; the device key never shows on a console or in
   a log and cannot be read back over the debug consoles.
5. Signed change net: a net with the right SHA-256 but a bad signature is refused (the per-device model download,
   `GET /v1/devices/{id}/model`, test130 steps 9-11).
6. Clips: RTMPS publish with credentials accepted; without, refused; the stream still starts within its budget.
7. Replay: a recorded check-in (`POST /v1/poll`) sent again is rejected (nonce or time window).
8. No clock: after a battery loss the board has no time until a server answer's `X-Time` (no NTP); that first TLS
   connection trusts the pinned server key, not the certificate's dates, and its `X-Time` sets the clock (DESIGN.md
   5.3.12 design decision) - defined behaviour, no plain fallback; a different key is refused. Then, with the clock
   set: an expired server certificate is refused (dates checked once a clock exists); a forged `X-Time` on a failed
   or plain exchange does not move the clock; before the first answer the clock reads >= the build time.

## Pass

All traffic is encrypted and authenticated both ways; every bad certificate, unknown device, bad signature and
replay is refused; the cost per wake is known and inside the battery budget.

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | n/a: TLS adds nothing to the boot (credentials parsed per session, not at start) | target (test022) | longest step |
| This part's boot share | n/a: 0 ms at boot; `psa_crypto_init()` at the first session | - | driver / mount / probe |
| Test run time | 52 s automated (run.py, 2026-11-10), no setup | - | setup separately |
| Speed / rate against the top | handshake 309-391 ms avg over 30 sessions (max 2266 ms, one Wi-Fi retry), TLS 1.3 + P-256 on the BL616's PKA/AES/SHA hardware; QUIC spike for comparison (DESIGN.md 5.3.12) | SPEEDS.txt | % of the top |
| Current idle / during (min / avg / peak) | n/a: no meter on the hands-free bench (leakcam-no-scope); the handshake adds ~0.3 s of radio to a 1-2 s check-in | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | n/a: no meter (bench limits) | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | n/a: no meter; estimate +0.3 s at the check-in's ~80 mA | - |  |
| Per day | n/a: no meter; 24 check-ins x ~0.3 s x ~80 mA = ~0.16 mAh/day estimate | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | n/a: a 0.3 s handshake heats nothing measurable (test140 has the board's) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | free 171,044-171,048 B steady over 8 sessions (by hand) and the 1st vs 10th session in run.py: no leak; session heap 15.9 KB | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles |
| Image (sizes) | BL616 test firmware 1326304 B (2026-11-10, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO (2026-11-10) | smallest code (rule) |  |
| Features over the minimal image | tls_link.c + mbedTLS ssl/PSA/x509: full image 1,223,728 B after the mbedTLS trim (1,252,912 untrimmed); static RAM unchanged with heap key slots (96 KB); session heap 15.9 KB, freed after each request | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |

## Boot time

TLS adds the handshake to each request in a session: per step against test101's session time (resumed and full).

## Battery energy

TLS energy per wake (handshake bytes and CPU) x wakes per day, against the plain-HTTP figures of test102 / test108.
