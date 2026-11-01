# test117: presence lost brings BLE back (the phone sets the Wi-Fi at once)

Status: **pass** (2026-11-01, 13/13; user: "loosing of presence for whatever reason re-enabled ble wake, so phone nearby
can reconfigure wifi instantly").

A LEAKCAM with a network stored stops advertising on battery while it is present at the server. When a check-in does
not reach the server, for whatever reason (the network gone, a wrong password saved without checking, no internet, the
server down), the presence is lost and BLE advertises again at once (provision.c: the first 60 s window starts at the
loss, then 60 s every 5 minutes, as for a board with no network), so a phone nearby finds it and sets the Wi-Fi. After
new credentials the board checks in at once (presence_now), not at the next interval.

## Setup

- Product firmware (firmware/bl616, make flash), the PIN stored; USB-C 1 in; the phone on adb, unlocked.
- The presence server on the Pi: the runner sets its poll interval to 10 s, stops and starts it, and puts back 60 s.

On USB power the board advertises anyway: the runner reads the battery rule the firmware drives its advertising with
(`prov`: "on battery on (presence lost)" / "off (configured, present)"), not the radio. The radio itself on battery:
test116's hand steps.

## Needs (build features and dependencies)

As test111: BLE provisioning, the Wi-Fi check, presence; the advertising rule (provision.c).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps (run.py)

1. Set up from the app, present: the battery rule off.
2. The server down: the rule on (presence lost) within one interval; the phone finds the board (its serial) at once;
   the server back: off again.
3. A missing network saved without checking from the app: the rule on at once; the app sets the real network at once
   (Done); the rule off, the board present at the server.

## Setup mode (DESIGN.md 5.3.1, planned)

- In setup mode (once test120 passed): a lost presence puts the board into low power with advertising (not the
  60 s windows); the app sets the Wi-Fi out of it; the first check-in that reaches the server makes it operational.
  A stored network is retried at the check-in interval from low power.

## Pass

Every cause of a lost presence switches advertising on within one interval (at once after new credentials), the phone
sets the Wi-Fi in one go, and a present board stops advertising on battery.

## Result

2026-11-01, 13/13 checks (poll 10 s, `prov battery on`): present -> the phone does not find the board; the server
stopped -> presence lost in 5 s, the phone finds the board (its serial) in 0.3 s; the server back -> silent again in
9 s; a missing network saved without checking -> lost in 4 s; the app sets the real network at once (Done 8.7 s) ->
silent again 2 s later, present at the server 14 s after that setup started.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 163 s automated (run.py, 2026-11-01), no setup | - | |
| presence lost -> advertising; set up again from the app | presence lost -> advertising: see the CHECK lines; set up again from the app 0.0 s | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 meter (LEAKCAM_METER=ask) |
| Voltage under load (min) | n/a: on USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | **MISSING** | - | |
| Per day | n/a: a fault case | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 145288 / min 120104 / 145288 B, blocks 107 -> 107, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-01) | no leak | |
| Image (sizes) | BL616 test firmware 1317888 B (2026-11-01, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 product (test compiles: no -O3/LTO) | smallest code (rule) | |
| Features over the minimal image | as test111 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: no K230 boot.

## Battery energy

Advertising after a loss costs the battery like the windows of a new board (test114 measures the cost per window).
