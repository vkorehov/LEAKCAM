# test140: temperature and humidity: the room after a cool window, the 85 %RH alarm (whole product flow, product firmware)

Status: **not run yet** (written 2026-11-10). First flash of the firmware with climate.c stopped the board before USB
(a heap mutex in metrics_boot(), before board_init(): HISTORY.md); fixed (static mutex), waiting for BOOT + EN.

DESIGN.md 5.3.15 (user, 2026-11-10: "measure temperature only after board cools down first thing after wake ...
3 hour window only and bigger. Humidity leak detection can still wake by simple >85 humidity"): the AHT20 every 10 min
(climate.c). The room's temperature and humidity only from a board cool for 3 h or more (no K230, no USB power: the
heat sources, test048); 85 %RH or more at any reading is a humidity alarm (a warm board only reads lower): a check-in
at once (the server's leak `humidity high`) and a K230 leak check with the reason `humid`.

## Setup

Product BL616 pair + product K230 image, USB-C 1 on the bench relay box (the BL616 console on its own port), the
server on the Pi. Hands-free: the relay box unplugs USB-C 1 (battery) and plugs it back; the bench window is 10 s
(`climate quiet 10`, until the next reset), the alarm's humidity simulated (`climate sim 90`); the phone acknowledges
its alert at once (TEST_ACK). The real sensor's accuracy and response against salt boxes: test041 (by hand).

## Steps

1. **USB power, warm:** `climate now` reads, the board `warm`, no room reading; the check-in carries `t`, `rh` and
   `quiet` ~0.
2. **Battery, cool:** USB-C 1 out; `climate` says `cool` once the K230 is off; after the 10 s window `climate now` is
   the room's (`: the room`, rooms +1); the next check-in carries `troom`/`rhroom` = that reading, `roomago`, `quiet`
   >= 10.
3. **A K230 session restarts the window:** `leak check` on battery (K230 on, DONE, off); `climate now` right after:
   cool < 10 s, no room reading.
4. **The alarm on battery:** 84 %RH no alarm, no leak at the server; 90 %RH: `alarm`, `leak: check humid: K230
   session`, the server's leak `humidity high` within seconds.
5. **USB back:** `warm` again; 90 %RH: `CHECK humid` to the running K230 (`job: CHECK humid` on its console). The
   window back to 3 h, the simulation off, the leak cleared.

## Pass

No room reading while the board is warm or before the window; the first reading after it is the room's and reaches the
server; 85 %RH or more at any reading opens the server's leak and runs a K230 leak check (session on battery, CHECK on
USB); below it nothing; the BL616 heap kept.

## Result

Not run yet.

## Needs (build features and dependencies)

BL616 full: climate.c (the timer, the window, the alarm, `climate`), metrics.c (the locked AHT20 read, `troom`,
`rhroom`, `roomago`, `quiet`), leak_check_now(). Server: leakIn `humidity high`, the room keys (main_test.go
TestHumidityLeak covers 84.9/85.0 and Not a leak). K230: the job's CHECK (any reason).

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: nothing boots in this test | test022 | |
| This part's boot share | n/a | - | |
| Test run time | 106 s automated (run.py, 2026-11-10), no setup | - | |
| Room reading | 27.8 C, 38.3 %RH after 10 s cool on battery (bench window) | - | after the bench window on battery |
| Humidity alarm | reading -> the server's leak in 2.9 s (on battery, K230 check `humid`) | at once | reading -> the server's leak |
| Current idle / during | n/a: not metered | floor (test031) | the 10-min reading: DESIGN 5.3.12 budget ~0.02 mAh/day |
| Memory BL616 / K230 (start / min / end), leak | BL616 free (176636, 157592) -> (176636, 157588) (free, min) | no leak | |
| Image (sizes) | n/a: see test137 | minimal image (rule) | |
| Speed / rate against the top | n/a | SPEEDS.txt | |
| Voltage under load (min) | n/a | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a | - | |
| Per day | n/a: test132 on battery | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | on USB: board 28.7 C (AHT20), BL616 die 24.3 C; on battery after 10 s cool: board 28.8 C; tdie 25.9 C | K230 limit (datasheet) | the room reading |
| Compile flags | BL616 -Os -g | smallest code (rule) | |
| Features over the minimal image | climate.c | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: nothing boots for the reading; on an alarm on battery the K230's session (test101's boot).

## Battery energy

The 10-min reading ~0.02 mAh/day (DESIGN 5.3.12); an alarm hour: test041 step 5.
