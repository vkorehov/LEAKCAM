# test107: change-net update from the server (GET /v1/nn)

Status: **planned** (2026-10-30): K230 test image with `leakcam_wake`, sessions and the Wi-Fi bridge from the BL616 test
firmware (`pt_session`, `pt_wifi`); the server runs on the Windows PC.

The server may answer a check with `"nn":"<CRC>","thr":[<cam0>,<cam1>]`: a new change net and/or new thresholds.
`leakcam_wake` downloads `GET /v1/nn/<CRC>`, refuses a body whose CRC-32 differs or that is over 4 MB, and saves it with
`nnstore` into the older of two slot files (`change.0` / `change.1`); the next wake loads the newest slot that checks,
else the factory kmodel with `CHANGE_THRESHOLD` (tools/test-standard/k230_apps/nnstore.h, leakcam_wake.c `nn_update`).

## Setup

- The mock server on the Windows PC (192.168.1.131): `mock_server.py --nn <kmodel>`, plus a fault mode to add (wrong
  body, cut connection, 404, oversized, slow).
- A second change net (retrained or the int8 net of test013 with other weights) and one corrupted copy that keeps a
  valid CRC but does not load on the KPU.
- K230 console (`/dev/ttyACM1`), the board on USB power for the steps, on battery for the energy.

## Needs (build features and dependencies)

K230: agent, `leakcam_wake`, `nnstore` / `refstore`, the KPU (test013), UFFS on /sdcard (test011, test028), the Wi-Fi
bridge (test100); server: the mock with `/v1/nn`.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. New net: the answer offers net B -> download, CRC checked, saved; the next wake's check carries `nn=<B>` and the
   thresholds; the change results use B.
2. Thresholds only (same CRC): no download, new thresholds saved and used.
3. Bad downloads, each leaving the current net in use: a body with a wrong CRC, a connection cut halfway, 404, a
   length over 4 MB, a server that stalls (the timeout ends it).
4. Power cut during the save (as test028, 20 cuts): every boot loads a valid slot (the new or the previous net), never
   none.
5. A net with a valid CRC that the KPU refuses: the wake falls back (previous slot or factory) and says so in its next
   check.
6. Both slots corrupt: the factory kmodel and `CHANGE_THRESHOLD`.
7. Time and energy of a download (size / bridge rate) and of the save; the wake that downloads still sleeps as the
   answer said.

## Pass

A net or thresholds offered by the server are in use from the next wake; no fault (bad body, cut, power loss,
unloadable net) leaves the device without a working net or with a net the server did not send.

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

A download adds to the session after the check (bridge rate, test100); the next wake's net load (from /sdcard) to
the first change result is part of the session time (test101).

## Battery energy

Once per update (rare): the download over Wi-Fi plus the NAND write, in mAh from test102's session current.
