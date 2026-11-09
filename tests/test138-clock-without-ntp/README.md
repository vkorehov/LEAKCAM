# test138: time without NTP (product pair: the server -> BL616 -> K230 clock chain)

Status: **pass** (2026-11-07 08:56, run.py 11/11, BL616 full + K230 0.6+g2961d6c-dirty): both clocks within 2 s of
UTC from the server alone (BL616 -0.4..-1.9 s, K230 0..-2 s), neither stepped for 3 s of difference, both stepped
beyond 5 s (the K230 to the BL616's next frame, the BL616 to the server's next answer), the K230's set from its
session's first frame.

The board's time comes from the server, not NTP (DESIGN.md 5.3.12, user 2026-11-07: TLS needs the date): every server
answer carries `X-Time: <unix s>`, the BL616 sets its clock from any exchange (wifi_check.c), every BL616 link frame
carries `;t=<unix s>` (k230_link.h), the K230 sets its clock from it. **Either clock changes only when it is more than
5 s off.**

## Setup

Product BL616 pair + product K230 image, USB-C 1 on the bench (the K230 on), the server on the Pi (bench check-in
interval 60 s). The bench PC's UTC (NTP) is the reference.

## Steps

1. **The server:** `X-Time` on every answer (a good one and an error), within 2 s of UTC.
2. **The BL616's clock:** `clock` shows `clock: <unix s> (set <n> times)`: within 5 s of UTC, set from the server.
3. **No step for small drift (BL616, natural):** two check-ins later (130 s) the count is the same.
4. **The K230's clock:** its shell's `date` within 5 s of UTC.
5. **No step within 5 s (K230):** the BL616's clock moved 3 s ahead (`clock <unix s>`), a BL616 frame (`leak check`:
   CHECK): no `clock:` line on the K230.
6. **A step beyond 5 s (K230):** the BL616 30 s ahead, a BL616 frame: `clock: <t> from the BL616 (was -30 s off)`.
7. **The server sets the BL616 back (> 5 s off):** the BL616 30 s ahead, its next check-in: set count +1, within 2 s of
   UTC; the K230 follows back at the next frame (`was +30 s off`).
8. **No step within 5 s (BL616):** 3 s ahead, the next check-in leaves it (count unchanged).
9. **The first frame carries it:** the session restarted (`k230 off`, the full firmware powers it again on USB): the
   K230's `clock:` line comes before its `WAKE` step.

The K230 shell's `date` only reads here: setting it there does not reach the running program's clock (2026-11-07),
so the K230's rule is driven from the BL616's clock.

## Pass

Both clocks within 5 s of UTC from the server alone, neither stepped for drift of 5 s or less, the K230's set from its
session's first frame.

## Needs (build features and dependencies)

Server: withTime (X-Time). BL616: wall_clock.c (with `clock`), wifi_check.c take_time, k230_link.c `;t=`.
K230: leakcam.c take_time (clock_settime). The BL616's HBN RTC keeping it through the sleep: with the sleep step
(test132).

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: the K230 runs already; step 7's restart is test113/test045's boot | test022 | |
| This part's boot share | n/a: one strtol and a compare per frame | - | |
| Test run time | 168 s automated (run.py, 2026-11-07), no setup | - | |
| Clock error BL616 / K230 against UTC | BL616 -0.4 .. -1.9 s, K230 0 .. -2 s (whole seconds) | <= 5 s | |
| Current idle / during | n/a: on USB power, not metered | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: no allocation in the clock path | no leak | |
| Image (sizes) | n/a: wall_clock.c ~0.2 KB | minimal image (rule) | |
| Speed / rate against the top | n/a: the clock rides on existing exchanges | SPEEDS.txt | |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: no extra exchange | - | |
| Per day | n/a: no extra exchange | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a | K230 limit (datasheet) | |
| Compile flags | BL616 -Os -g; K230 program -Os | smallest code (rule) | |
| Features over the minimal image | wall_clock.c, the X-Time parse, `;t=` (BL616); take_time (K230) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
