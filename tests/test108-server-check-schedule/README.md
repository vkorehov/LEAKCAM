# test108: the server drives the check-in and leak check intervals (whole product flow, product firmware)

Status: **pass** (2026-11-07 10:15, run.py 10/10): both intervals follow the server per device within one check-in
(check-in 20 s taken in 41-54 s, then 21 s apart; leak check 60 s taken in 14-22 s), kept across a reset with the
server down (HBN RAM, step 2b: added after test128 found the 60 min default after a crash), the RTC leak check on its
interval (57-61 s at 60, CHECK to the running K230 on USB), back on the defaults with PUT 0.

DESIGN.md 5.3.12 (user, 2026-11-07: "make check-in time driven by the server and the leak check also, so we can adjust
it dynamically"): every check-in's answer carries `"checkin":<s>` (5 s - 1 h, default 3600; the bench server 60) and
`"leakcheck":<s>` (10 s - 7 days, default 86400); `PUT /v1/devices/<id>/intervals` changes them per device at runtime
(0 = the default, kept in the server's memory); the board takes them with its next check-in. The leak check runs on its
interval by itself (the RTC's check: a K230 session on battery, CHECK to the running K230 on USB).

## Setup

Product BL616 pair + product K230 image, USB-C 1 on the bench (USB power: the K230 on), the server on the Pi
(deploy.sh 60). No hand, no phone. The battery side (the K230 off after the check, the HBN sleep between) is test132.

## Steps

1. **The defaults:** the board's `presence` says `check-in every 60 s` (the bench server's default), `leak` says
   `every 86400 s`.
2. **The check-in interval per device:** `PUT {"checkin":20}`: within one old interval the board says `check-in every
   20 s`, and its check-ins come ~20 s apart (the server's `last_seen_s`).
2b. **Kept across a reset:** the server down, the board reset: it still checks in every 20 s (the intervals are kept in
    HBN RAM: a reset with the server out of reach must not fall back to the 60 min default; test128 2026-11-07).
3. **The leak check interval per device:** `PUT {"leakcheck":60}`: after the next check-in `leak` says `every 60 s`;
   within ~60 s a leak check runs by itself (`leak`: checks +1; the K230 console `job: CHECK rtc`), DONE back, the
   K230 still on (USB).
4. **Again, on its interval:** the next one 60 s +- 5 s later.
5. **Back to the defaults:** `PUT {"checkin":0,"leakcheck":0}`: the board back on 60 s / 86400 s.

## Pass

Both intervals follow the server per device within one check-in, the leak check runs on its interval without a hand.

## Needs (build features and dependencies)

Server: Intervals, PUT /v1/devices/{id}/intervals, the answer's checkin/leakcheck (server/main_test.go covers the
limits and 0 = default). BL616 full: presence.c answer_value, leak_check_every, the RTC check in leak_check.c. K230:
the job's CHECK.

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: nothing boots in this test | test022 | |
| This part's boot share | n/a | - | |
| Test run time | 272 s automated (run.py, 2026-11-07), no setup | - | |
| Interval taken: PUT -> the board on it | check-in 20 s taken 54 s after the PUT; check-ins then [21, 21] s apart | <= one old interval | |
| Leak check on its interval (spacing) | 58 s between two RTC checks at leakcheck 60 | 60 s +- 5 s | |
| Current idle / during | n/a: on USB power, not metered | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free (184056, 164676) -> (184056, 164676) (free, min); the K230 job's heap: test137 | no leak | |
| Image (sizes) | n/a: see test137 | minimal image (rule) | |
| Speed / rate against the top | n/a: intervals, not speed | SPEEDS.txt | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: on USB power; the budget is DESIGN.md 5.3.12's (24 check-ins + 1 leak check a day) | - | |
| Per day | n/a: test132 measures it on battery | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a | K230 limit (datasheet) | |
| Compile flags | BL616 -Os -g; K230 program -Os | smallest code (rule) | |
| Features over the minimal image | presence.c intervals, leak_check.c RTC check | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
