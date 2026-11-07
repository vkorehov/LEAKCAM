# test103: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.8, 5.3.12)

The first plan followed the first product (attic/firmware): `leakcam_wake` on the K230 posting `POST /v1/check` and
choosing the next sleep, and a humidity "sensor alarm". The rewrite: the K230's job reports `DONE,<cam0>,<cam1>`, the
BL616 carries the result in its check-in (`job=`, `jago=`) and checks in at once on a change or a wet probe; the
intervals come from the server (`checkin`, `leakcheck`); no humidity alarm (AHT20 reported only). The old steps:

    1. Wake -> K230 READY (test101) -> capture both cameras -> change net -> history -> POST /v1/check -> answer ->
       (leak) RTMP clip start: timestamps at each step on the K230 console (console_record.py).
    2. Same with the server answering "leak" (the clip path) and "no leak" (back to sleep).
    3. The decisions `leakcam_wake` takes on the board: same / light / new per camera against the history, a sensor alarm
       without a visible change, no server, server not reached; the sleep it sends each time (6 h / 1 h / 10 min).
    4. Battery cost: with test102's current, the energy per wake for each path.

## Runs

None: not run yet.

## Obstacles

None.
