# test109: secure server (TLS, device identity, pinned certificate, signed change net)

Status: **planned** (2026-10-30): the real server is not built (DESIGN 7, SECURITY.txt 4); it will run on the Windows PC for the test.

Today every request is plain HTTP / RTMP without authentication (SECURITY.txt 4). Needed before real use: TLS for
`/v1/check`, `/v1/nn`, `/v1/poll` and the clips (RTMPS to MediaMTX); a per-device identity so the server knows who
posts; the server's certificate pinned on the device; the change net checked against a signed value. This test
proves each and measures what TLS costs on a battery device.

## Setup

- The server on the Windows PC (192.168.1.131): HTTPS for the three requests and MediaMTX with RTMPS and publish
  authentication; a test CA, the server certificate, device credentials; switches for a wrong / expired /
  other-host certificate, a revoked device and a replayed request.
- The board on battery for the energy steps, on USB for the rest; the K230 console and the BL616 console.

## Needs (build features and dependencies)

K230: TLS client for the three requests and RTMPS (library to choose: mbedTLS in the SDK, or what RT-Smart offers),
the device key storage, time from NTP for certificate dates; BL616: TLS for `/v1/poll` **if it fits** - Wi-Fi RAM is
at 99.6 % (skill leakcam-product-build 7): step 1 decides whether the BL616's own check can use TLS at all.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Fit: TLS on the BL616 (heap and static RAM for one handshake next to Wi-Fi and BLE) and on the K230 (RAM, image
   size). If the BL616 cannot, the decision is written down (the poll over TLS through the K230, a signed plain
   answer, or no BL616 poll).
2. Handshake cost: full handshake and session resumption per request - time, bytes on air, energy (it repeats on every
   wake: resumption or a kept ticket matters).
3. Pinned certificate: a wrong CA, an expired certificate, another host name, a downgrade to plain HTTP: the device
   refuses each, sends nothing of the owner's, backs off.
4. Device identity: the server refuses an unknown and a revoked device; the device key never shows on a console or in
   a log and cannot be read back over the debug consoles.
5. Signed change net: a net with the right CRC but a bad signature is refused (test107's update path).
6. Clips: RTMPS publish with credentials accepted; without, refused; the stream still starts within its budget.
7. Replay: a recorded `/v1/check` sent again is rejected (nonce or time window).
8. No clock: the first wake after a total power loss (no RTC time) still validates the certificate or waits for NTP -
   defined behaviour, no plain fallback.

## Pass

All traffic is encrypted and authenticated both ways; every bad certificate, unknown device, bad signature and
replay is refused; the cost per wake is known and inside the battery budget.

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median / max of 5) | **MISSING** | target (test022) | longest step |
| This part's boot share | **MISSING** | - | driver / mount / probe |
| Test run time | **MISSING** | - | setup separately |
| Speed / rate against the top | **MISSING** | SPEEDS.txt | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | probe points named |
| Power, energy per operation | **MISSING** | - |  |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) |  |
| Features over the minimal image | **MISSING** | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays |  |

## Boot time

TLS adds the handshake to each request in a session: per step against test101's session time (resumed and full).

## Battery energy

TLS energy per wake (handshake bytes and CPU) x wakes per day, against the plain-HTTP figures of test102 / test108.
