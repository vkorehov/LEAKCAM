# test125: setup mode retries presence every 6 h and returns to the full firmware

Status: **planned** (2026-11-01, user: "after entering setup mode, we still would like to check if presence restored
once a day and then switch back", high level; then "in case loss of presence we retry every 6h"). Design: DESIGN.md 5.3.1 (a stored network is retried every 6 h).

A board whose presence was lost (the router off, the internet down, the server unreachable) is in setup mode: the
setup firmware sleeps and advertises BLE. Its stored network may come back by itself, so every 6 h the setup firmware
switches to the full firmware, which tries its normal check-in: reached, presence is back and it stays (no user
action); not reached, it switches back to the setup firmware at once (user, 2026-11-01: the setup firmware has no
check-in of its own). The whole cycle end to end: operational -> presence lost -> setup -> presence back -> operational.

## Setup

- Both firmwares in their slots (the setup firmware and the full firmware: the slot switch built, DESIGN.md 5.3.1), the
  presence server (server/), the app on the phone.
- A network the bench controls (the phone's hotspot or a second AP that can be switched off), stored on the board.
- The retry period shortened in a test build (e.g. 5 min instead of 6 h) for the cycle steps; one real 24 h run at
  the end (unattended, the board on the cell).

## Needs (build features and dependencies)

Setup firmware: the 6 h timer across PDS sleep (RTC), the slot switch to the full firmware. Full firmware: its
check-in at boot, presence loss -> the slot switch back to the setup firmware at once (test117's trigger). Server and app as in test117/test118 (online / offline / needs setup).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Low level, the retry alone (network off): every period the setup firmware switches to the full firmware, which
   tries one check-in, fails (network not seen) and switches back; the time in the full firmware, the energy per
   attempt (two boots + one check-in), the partition table written twice; no attempt between periods.
2. Presence back: the network switched on between two attempts: at the next attempt the check-in reaches the server,
   the full firmware's check-in reaches the server and it stays (no switch back), then checks in at its own interval; the app shows the board online again (UI Automator); time from the network's return to online.
3. The whole cycle twice: operational -> the network off -> presence lost (test117: setup firmware) -> the network on
   -> back to operational at the next attempt (within 6 h); BLE behaves as each mode says (setup: low-power advertising,
   operational: off on battery).
4. Not back: the network stays off for several periods: the board stays in setup mode, one attempt per period, the
   app keeps showing "needs setup"; a phone can still set a new network over BLE at any time (that ends setup mode at
   once, without waiting for the attempt).
5. Edge cases: the network back but without internet (the check-in fails: stays in setup); the server back but the
   network changed (the stored one never works: stays in setup, the app offers setup); USB plugged during setup (the
   full firmware takes over, test121) and unplugged again with presence still lost (back to setup).
6. The real period: 24 h on the cell, the network off, then on: exactly four attempts per day (the server's log), the
   board back to operational within 6 h of the network's return; the day's energy (sleep + four attempts).

## Pass

A board in setup mode finds its stored network again by itself within 6 h and returns to the full firmware with no
user action; it never tries more than once per period; a new network from a phone or a USB plug still works at any
time.

## Result

Not run.
