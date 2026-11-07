# test104: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The first plan budgeted the first product's day: 144 humidity slices (a BL616 wake every 10 min, ~85 ms awake) and 4
K230 report sessions a day on the BL616 test firmware. Humidity wakes are gone (AHT20 reported only) and the day is
now the full firmware's: HBN sleep between events, 24 check-ins (the server's `checkin`, default 3600 s) and 1 leak
check (`leakcheck`, default 86400 s), ~2.85 mAh/day estimated. The old lines:

    Status: **planned** (2026-10-30): when the BL616 test firmware runs the whole wake cycle for days (hibernate, sessions, Wi-Fi).

    2. Energy per humidity slice, calculated: its awake time (test021, ~85 ms) x the BL616 awake current (test031,
       test040) (no battery measurements (user, 2026-10-30)).
    3. Energy per K230 report session: current over the whole session (test101 timing, test102 current), boot part
       separate (test022).
    5. Budget: sleep + 144 slices + 4 reports per day (+ leaks), self-discharge; battery life for 1 and 2 cells.

## Runs

No runs yet.

## Obstacles

None.
