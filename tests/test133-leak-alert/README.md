# test133: the app's leak alert, emergency style

Status: **pass, 13/13** (2026-11-07 18:08): locked phone -> alert posted 0.25-1.7 s after the trigger, full screen over
the lock screen 0.4-2.0 s, the alarm-usage sound started; in silent mode and in Do Not Disturb (priority) the same.
End to end through the server (a wet-probe check-in -> FCM push -> the full screen on the locked phone, the app
closed): 1.7 s (by hand, 2026-11-07 18:03). Each test alert rings 0.5 s, then the runner acknowledges it (TEST_ACK)
and checks from the logs.

DESIGN.md 5.3.13 (user, 2026-11-07): the leak alert is the app's most important use case. Android's government
alerts (cell broadcast) are not open to apps, so the app shows the closest: a full-screen alert over the lock screen,
the alarm sound repeating until acknowledged, vibration, through silent mode and Do Not Disturb. Every user-facing
step well under 15 s (5.3.4).

## Setup

The bench phone on adb (Wi-Fi), the debug app (phone.ready() installs it and grants the alert's permissions:
notifications, full-screen intent, DND access). No board: the trigger is the debug build's broadcast
`com.leakcam.setup.TEST_ALERT`; the server's push (FCM) replaces it once the Firebase project exists.

## Needs (build features and dependencies)

App: LeakAlert.kt, AlertActivity.kt, MainActivity.kt (alertPermissions), AndroidManifest.xml. Tools: phone.py.

## Steps

1. **Locked:** the phone locked, the screen off; the trigger, then TEST_ACK 0.5 s later (a test alert rings no
   longer: user rule); from the logs: the alert posted, AlertActivity started over the lock screen within 15 s, a
   USAGE_ALARM player started; nothing left after the acknowledgement.
2. **The channel:** importance high, bypasses Do Not Disturb, the alarm sound.
3. **Silent mode** (ringer silent): step 1 again, the sound still plays.
4. **Do Not Disturb on (priority mode, the switch people use):** step 1 again, the alert and the sound still come.
   Total silence (zen 2) blocks alarms for every app: not a case an app can pass.

## Pass

All three cases: the full screen within 15 s over the lock screen, the sound until acknowledged, gone after it.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: no board | target (test022) | |
| This part's boot share | n/a: no board | - | |
| Test run time | 83 s automated (run.py, 2026-11-07), no setup | - | |
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
