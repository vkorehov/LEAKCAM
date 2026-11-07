# test135: the app's screens, status block, pending list and leak flow

Status: **pass, 35/35** (2026-11-07 22:26 UTC+2): no boards -> Setup 1.5 s, the menus, no search on LEAKCAMs and
Change Wi-Fi, Setup skipping a known board, the next check-in in every status, the pending list (Wi-Fi > photo > video,
one each, buttons off), the action row unmoved, the alert (probe / inference, no signal) and its picture 0.5 s after the
upload, Not a leak 3.0 s, a link opening Samsung's Gallery on the file alone, the last board deleted -> Setup.

DESIGN.md 5.3.4, 5.3.13, 5.3.14 (user, 2026-11-07): the screens LEAKCAMs / LEAKCAM setup / Change Wi-Fi, the
three-lines menu, the action row + status block building block, the next check-in time in every status, the server's
pending list (one photo, one video, one Wi-Fi change; Wi-Fi > photo > video), Setup for new devices only, the leak
alert (probe / inference words, no signal, the first picture embedded), the leak panel (single file, +N more, Not a
leak), the photo and video links.

## Setup

The phone on adb (no hand, no cable), the server on the Pi. The app runs as a throwaway phone (debug
`leakcam_phone_id`) with a fake board, so the person's list is never touched; the real board only for the "Setup skips
a known board" case (this phone added as one more user of it for that case, removed after). Alerts post on the quiet
channel (`leakcam_quiet`) and are acknowledged at once.

## Needs (build features and dependencies)

App: MainActivity (screens, StatusBlock, pending, media links, notLeak), LeakAlert (quiet channel, picture),
PushService (phoneId override, media push), Gallery (view). Server: pending, next_checkin, media upload + list,
not_leak. Tools: phone.py ready.

## Steps

1. **No boards -> Setup at once:** title "LEAKCAM setup", the status block's spinner line "Looking for LEAKCAMs
   nearby…", blank fields, scan and cable watch on; the menu holds only Setup.
2. **A board -> LEAKCAMs:** title, the row "serial · SSID", the status text with "next check-in"; no scan, no cable
   watch; the menu holds LEAKCAMs and Setup.
3. **Setup skips a known board:** the real board added to this phone: not listed while Setup looks (conclusive when
   step 1 saw it advertise).
4. **Change Wi-Fi:** its own screen, no search, the SSID filled in, the password as dots; the same network without a
   password refused; a new one sent through the server -> back on LEAKCAMs, the note with the next check-in time.
5. **Photo and video:** queued, their buttons off, the pending list in the server's order (Wi-Fi, photo, video); the
   check-ins deliver them one by one in that order; the action row never moves.
6. **Leak alert:** a wet probe -> the alert with "probe", no signal; the first picture's upload -> embedded; the camera
   inference -> "Possible leak" and "inference".
7. **Leak panel:** Not a leak -> cleared at the server, the note "watches again from its next check-in (…)".
8. **Links:** the uploaded files listed; a tap opens the file alone in the phone's viewer.
9. **The last board deleted -> Setup.**

## Pass

Every check; every user-facing step under 15 s.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: no boot in this test | target (test022) | |
| This part's boot share | n/a: no board | - | |
| Test run time | 141 s automated (run.py, 2026-11-07), no setup | - | |
| Speed / rate against the top | slowest user-facing step: Change Wi-Fi send -> LEAKCAMs 3.0 s, Not a leak 3.0 s (taps included; the most a person waits: 15 s); empty -> setup 1.5 s, leak -> alert 0.5 s, picture -> alert 0.5 s, delete -> setup 0.1 s | 15 s the most a person waits | slowest user-facing step |
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
