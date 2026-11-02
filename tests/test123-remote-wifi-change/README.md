# test123: change the Wi-Fi network remotely, back to the previous one if the new fails in 15 s

Status: **planned** (2026-11-01, user: "user may want to switch wifi network without going to device, remotely, in case
he has second wifi nearby. Support falling back to previous working WIFI, if new wifi did not work out in 15
seconds"). Design: DESIGN.md 5.3.2.

A board with presence (operational, full firmware) is moved to another network from the app: the app hands the new
SSID and passphrase to the server, the board takes it with its next check-in, checks it (join, address, check-in over
the new network) within 15 s and stores it, or goes back to the previous network and reports why.

## Setup

- Full firmware (firmware/bl616_full), the presence server (server/) with the pending-change endpoint, the app.
- Two networks the bench can reach: the current one (A) and a second one (B, e.g. the phone's hotspot or a second
  AP); credentials from ~/.leakcam_wifi (never on a command line, never printed: memory leakcam-wifi-credentials-file).
- The board on battery or USB with presence on A; the phone on adb with the app.

## Needs (build features and dependencies)

Server: a pending change per device (attempt id, SSID, encrypted passphrase), delivered in the check-in answer,
dropped after delivery; the result kept by attempt id. BL616: the check-in answer parsed (presence.c), wifi_check.c
with a 15 s budget, the previous network kept (`wifi_*_prev` in easyflash), the result in the next check-in. App: the
"change Wi-Fi" flow for a board online, the result by attempt id.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Low level, the board alone: a pending change injected at the server for network B: the next check-in takes it,
   joins B, checks in over B within 15 s; easyflash holds B as current and A as previous; the server has the result
   (saved) for the attempt id. Timings: answer -> join -> address -> check-in.
2. Fallback, wrong password for B: back on A within 15 s + the rejoin; the result "wrong password"; easyflash still
   A, nothing stored; presence never lost for longer than one check-in interval.
3. Fallback, B not in range (a made-up SSID): result "network not seen", back on A.
4. Fallback, B without internet (an AP with no uplink, or the server unreachable from B): result "no internet", back
   on A.
5. Loss in the middle: the board reset while it tries B (`reboot` or the watchdog): it comes back on A (the stored
   network), the attempt reads "not done"; no half-stored credentials.
6. During a K230 session: the change waits for the session's end, then runs.
7. From the app: the user picks "change Wi-Fi" on an online board, enters B; the app shows "moved to B", or "kept on A"
   with the reason, by attempt id (UI Automator).
8. Security: the passphrase never in clear in a capture of the check-in (tcpdump on the Pi) nor in the server's
   storage after delivery; a change request without the app's authorization is refused (once 5.3.2's open point is
   built).

## Pass

A change to a working network moves the board within 15 s; every failure leaves it on the previous network with the
right reason and nothing stored; a loss in the middle never strands the board; the passphrase never crosses or rests
in clear.

## Result

Not run.
