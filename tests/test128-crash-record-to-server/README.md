# test128: crash records reach the server over Wi-Fi as soon as Wi-Fi is available

Status: **steps 1 2 3 5 7 pass** (2026-11-07 on the DESIGN 5.3.12 firmware, after the interval fix: HISTORY; step 5
rewritten for the product setup firmware: the record went with its check-in at 08:31:29 UTC, the full firmware's first
check-in 7 s later); 2026-11-02 first pass (steps 4, 6, 8 open; user: "add test case to always send crash records", then "send it over wifi only",
"once available"). Design: DESIGN.md 5.3.3.

A crash record (any kind: exception, watchdog hang, assert, stack overflow, malloc failure) is sent to the server by
the first firmware that reaches the LEAKCAM API over Wi-Fi after it, with the build that wrote it; the board clears it
only once the server confirmed it. Nothing over BLE or USB.

## Setup

- The pair flashed (full in slot 0, the setup test bed in slot 1), the presence server (server/) on the Pi with the
  crash endpoints (its check-in interval 60 s, the bench default; the product default is 3600 s, set per device by
  `PUT /v1/devices/<id>/intervals`), a network stored; USB-C 1 in (console).
- Crashes made by command: `crash test` (exception), `crash hang` (watchdog hang).

## Needs (build features and dependencies)

BL616: crash_rec.c (the record, its build, a "sent" handshake), the full firmware's check-in (presence.c: at boot at
once when a record is kept), the setup firmware's API step (wifi_check.c, api.c). Server: POST
/v1/devices/{id}/crash, GET /v1/devices/{id}/crashes, kept per device, a resend (the same record text) once. Host: crash_decode.py with the
kept ELF.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Full firmware: `crash test` -> after the reset its first check-in sends the record; GET .../crashes shows it with
   the build and count; `crash` on the board then says no record. Time from the reset to the server having it.
2. Watchdog hang: `crash hang` -> the same, kind "watchdog hang".
3. Server down at the first check-in (`systemctl stop leakcam-server`): the record stays on the board; server back
   -> the next check-in sends it; once only on the server (no duplicate for a resend).
4. Lost answer: the server stores it but the board does not see the answer (the server stopped right after storing,
   or the link dropped): the board sends it again, the server keeps one record (same key).
5. Crash in the full firmware, then the switch to setup (`slot switch`) before any check-in: the setup firmware sends
   it with its next API step (`switch_to_full usb` or a BLE setup), with the full firmware's build.
6. No Wi-Fi at all (no network stored, setup firmware): the record stays, still printed at boot and by `crash`; sent
   once a network reaches the API.
7. Two crashes before a send: the server gets the last record with count 2.
8. Decode on the host from the server's copy: crash_decode.py with the kept ELF names the right function.

## Pass

Every crash reaches the server once, with its build, at the first Wi-Fi contact with the API, from either firmware;
the board clears it only after the server confirmed it; never over BLE or USB.

## Result

- 2026-11-02 07:04-07:10 UTC, `run.py` (full 0.6+g1d2b375-dirty-202611020642 in slot 0, setup_test ...0643 in slot
  1; the server with crash records on disk, deployed 07:04): **PASS**
  - step 1, exception: at the server 4.1 s after the console was back, cleared on the board; decoded from the server's
    copy with the kept ELF: cmd_crash (the test's illegal instruction).
  - step 2, watchdog hang: the same, 4.1 s; decoded cmd_crash (the test spins in it).
  - step 3, server stopped: kept on the board; sent at the next check-in 36.8 s after the server was back (the bench
    server's 60 s check-in interval), one copy.
  - step 5, server stopped across a crash in the full firmware and the switch: the setup firmware held the full
    firmware's record and sent it with its API step (`switch_to_full usb`) 9.1 s later, build "full ...", decoded
    cmd_crash; then the hand-over to the full firmware.
  - step 7, two crashes before a send (server stopped): one record with count 2.
- The record goes over the API (POST /v1/devices/{id}/crash, a form) right after the check that reached it: the full
  firmware's check-in (5 s after its start), the setup firmware's API step (BLE check, hand-over); never BLE or USB.
- 2026-11-02 17:57 UTC, steps 1-2 again on the full firmware without BLE (0.6+g1d2b375-dirty-202611021742): **PASS**,
  each at the server 4.1 s after the console was back, decoded cmd_crash.
- **To rework (design 2026-11-02):** steps 3, 5 and 7 stop the server, and a failed check-in now switches the full
  firmware to the setup firmware ("lost", tests/test117): the record then waits in the setup firmware, which reaches the
  API only with a USB plug check or a BLE setup, or goes back after 6 h. The steps keep the board in the full firmware
  only with the server answering; a lost record across the switch is its own step.
- A record written after a PDS sleep counts its uptime from the last wake (the setup firmware's wake restarts the mtimer,
  tests/test124): "at 12017 ms" for a hang ~300 s after the start.
- Open: step 4 (a lost answer: the server stores, the board misses the 200), step 6 (no Wi-Fi at all), step 8 is
  covered by the decodes above.

## Metrics

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | run.py steps 1 2 3 5 7: 6 min; steps 1 2: 21 s | - | no hand step |
| crash -> record at the server | 4.1 s after the console was back (exception and hang); 36.8 s after the server came back (the next check-in, the bench server's 60 s interval) | one check-in (the server's `checkin`: product default 3600 s, per device by PUT /v1/devices/<id>/intervals) | console back 11.1-11.3 s after the crash (reset + VMware attach) |
| Current idle / during (min / avg / peak) | n/a: on USB power, no meter reading | - | |
| Voltage under load (min) | n/a: on USB power | - | |
| Power, energy per operation | one extra HTTP POST per crash (a form of ~300 bytes) after the check-in | - | nothing when there is no record |
| Per day | n/a: only after a crash | - | |
| Temperature die / board (start / peak / end) | n/a: K230 off, the BL616 at idle load | - | |
| Memory BL616 / K230 (start / min / end), leak | n/a: each step one crash and one reset (the heap starts fresh) | - | |
| Image (sizes) | full 1086 KB (no BLE), setup test bed 1284 KB | slot sizes | |
| Compile flags | BL616 -Os -g (SDK default), no LTO | smallest code (rule) | |
| Features over the minimal image | api.c `api_crash_target`, the POST in wifi_check.c, crash_rec.c `crash_rec_get`/`crash_rec_sent` | - | |
| Optimised build (-O3, LTO, -march) | n/a | - | |
