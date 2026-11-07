# test134: a Wi-Fi change through the server

Status: **pass, 8/8** (2026-11-07 20:16): a wrong passphrase refused (0x11), the old network kept, the failure at the
server, the 5-min wake mode; the right one tested and stored (0x04) 2.2 s after the check-in, saved at the server, the
mode left.

DESIGN.md 5.3.13 (user, 2026-11-07): the person types the new network in the app, which sends it to the server; the
board takes it from its next check-in's answer, tests it with the whole check (join, address, internet, the API) and
stores it only when it passes, else keeps the old one; the result in its next check-in (`wchg`), a failure notified to
the phone and the board in the 5-min wake mode (check-in every 300 s).

## Setup

The product pair, the board set up on the bench network; the server on the Pi. No hand, no phone.

## Needs (build features and dependencies)

BL616: presence.c (wifi_change, answer_string), metrics.c (wchg), wifi_check.c. Server: alert.go (putWifi,
checkInAlerts, wakeMode). Tools: phone.py intervals, board.py wifi_credentials.

## Steps

1. **A wrong passphrase:** PUT the bench SSID with a wrong one; a check-in now -> the board's `the new Wi-Fi: state
   0x11` (wrong password), the old network kept; the server's `wifi_change` failed; the board's check-in every 300 s
   (the 5-min wake mode); still online.
2. **The right one:** PUT again with the right passphrase -> state 0x04 (saved), stored, within 15 s of the check-in; the
   server's `wifi_change` saved, the mode left (the run's 5 s check-in again).

## Pass

Both results as above; the board online all the time; nothing of the passphrase printed.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: no boot in this test | target (test022) | |
| This part's boot share | n/a: no board | - | |
| Test run time | 37 s automated (run.py, 2026-11-07), no setup | - | |
| Speed / rate against the top | trigger -> full screen 0.6-2.0 s (the most a person waits: 15 s) | 15 s the most a person waits | trigger -> full screen |
| Current idle / during (min / avg / peak) | n/a: the phone only | floor (test031) | |
| Voltage under load (min) | n/a: the phone only | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: the phone only | - | |
| Per day | n/a: an app test | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: no board | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: no board | no leak | |
| Image (sizes) | n/a: the app only | minimal image (rule) | |
| Compile flags | n/a: the app only | smallest code (rule) | |
| Features over the minimal image | n/a: the app only | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no board.

## Battery energy

n/a: an app test.
