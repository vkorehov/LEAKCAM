# test139: USB-C 1 replug stability (no timing wedges the BLE core)

Status: **pass** (2026-11-09: A-D 26/26 at 04:19 UTC in 10.4 min, E 8/8 at 04:31 UTC, product pair with the wedge detector): no plug timing wedged the BLE core; found asleep after every case in 3.8-6.8 s.

No way of plugging or unplugging USB-C 1 may leave the BL616's BLE core wedged (user, 2026-11-09: "nothing should
produce wedge in a product"). On 2026-11-08 the board stopped advertising in the setup firmware's sleep on the cell,
on every firmware, until a power loss (skill bl616-ble-low-power, case H); the trigger was not found by hand. The
bench relay box now plugs and unplugs without hands, so this test sweeps the plug timing across the setup firmware's
states, and proves the core alive after every case.

## Setup

Product BL616 pair (firmware/bl616_full slot 0, bl616_setup slot 1, both with the wedge detector: `evlog` WEDGE
entries and register dumps, `blecore`), the cell in, USB-C 1 through the bench relay box (skill leakcam-bench-relays),
the bench phone on adb with the test APK, the server on the Pi.

## Needs

The relay box's timed phases (phone.relay() gaps: the relay app com.relay, `wait:<ms>` between AT+MPIO writes on one open connection; each write also
waits for its reply, about one BLE connection interval), phone.wedge() (the precondition of every BLE test),
run_tests.product_start() (the start state back at the end).

## Steps (run.py)

1. Precondition: no wedge (phone.ready()). The network erased: the setup firmware on USB; the evlog cleared.
2. **A (the unplug path):** unplug, plug back after 100, 300, 600, 900, 1000, 1100, 1300, 2000, 3000, 6000 ms (the
   K230 stop, the advertising restart at ~4 ms, the sleep at 1000 ms, advertising every 2 s), 3 s plugged, unplug.
3. **B (the plug wake):** asleep 8 s, plug, unplug after 50, 150, 400, 800, 1500, 3000 ms (the PGOOD wake, USB back,
   the K230 session start at 0.1-1.6 s).
4. **C (bursts):** 10 replugs 100 ms apart, 5 at 200 ms, 5 at 500 ms, 3 at 1000 ms, ending unplugged.
5. **D (the firmware switch):** `slot switch` to the full firmware (no network: it starts the setup firmware again
   on the cell) with the unplug landing around it (the offset in the evlog).
6. **E (half-plugged, the relay bring-up of 2026-11-08 18:42-18:49, the last thing before the first wedge):** CC
   alone 10 s; VBUS alone 10 s with D+/D- still on the PC; D+/D- 0.5 s before VBUS; VBUS first; the wires one by
   one 0.5 s apart; a swap to the phone (it powers and enumerates the board) 10 s; to the phone and back to the bench.
7. After every case: unplugged, the phone finds the board asleep (30 s; D 120 s); plugged, no WEDGE in the evlog, the
   core alive (`blecore`), the setup firmware answering. A failure stops the run with the evlog in the log.
8. The bench network set again over the cable, the full firmware (the suite's start state).

## Pass

Every case found asleep afterwards, no WEDGE, the core alive.

## Result

- **No wedge from any plug timing tried** (2026-11-09): A (replug 155-6058 ms after the unplug: across the K230 stop,
  the advertising restart, the 1 s before the sleep, the sleep), B (unplug 50-3060 ms after a plug from asleep: the
  PGOOD wake, USB back, the K230 session start), C (bursts: 10 x 180 ms, 5 x 270 ms, 5 x 570 ms, 3 x 1050-1080 ms),
  D (an unplug before the switch, inside the full firmware's ~200 ms life or the setup firmware's boot, and 1.0 s and
  3.0 s after the setup firmware's boot). Every case: found asleep, no WEDGE, the core alive.
- Measured spacing = the gap + 50-80 ms (the relay box's reply); 100 ms asked = 150-181 ms. Shorter replugs than
  ~150 ms need the HM-10 itself to time them.
- The full firmware with no network goes back to the setup firmware ~200 ms after its boot (evlog), so an unplug
  inside the full firmware is luck of the relay's connect time (3-5 s); D's real offsets are in the evlog lines of
  the run log.
- E (2026-11-09, 7/7): no half-plugged state wedged it either. One first run of E vbus-only failed after the plug
  with the core fine: the live check read 0x28000100 = 0 right after the plug wake (a healthy core reads 0 then);
  phone.wedge() now reads twice 1 s apart.
- The 2026-11-08 wedge's trigger is still not found (not in this sweep, not test116's battery switches, not a phone
  session in the sleep window); left: ~10 h awake on USB in the setup firmware (no network, 100 ms advertising, the K230 on: 08:01-18:42 UTC that
  day), a night on the cell, test110's decoy loop. run_tests.py now checks for a wedge after every test
  (`--wedge-probe`: a sleep probe too).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 626 s automated (run.py, 2026-11-09): 25 cases + the erase + the cable restore | - | |
| replug cases: found asleep after each | 25 cases in 10.4 min; found asleep after each: median 4.6 s, max 5.6 s (2026-11-09) | - | |
| Current idle / during (min / avg / peak) | n/a: on battery (no battery-side probes, bench limits) | floor (test031) | |
| Voltage under load (min) | n/a: no battery-side measurement on the bench (memory: bench limits) | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: a stability test, no energy per operation | - | |
| Per day | n/a: not a daily operation | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: K230 off most of the time, the BL616 at idle load | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 136148 / min 127048 / 136148 B, blocks 101 -> 101, deleted 0; leak: n/a, 1 snapshots (mem_leak.py needs >= 8 cycles) (2026-11-09) | no leak | |
| Image (sizes) | BL616 test firmware 1326272 B (2026-11-09, on the board at the run) | minimal image (rule) | |
| Compile flags | BL616 product -O3 + LTO | smallest code (rule) | |
| Features over the minimal image | as test116 | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

n/a: the BL616 alone; its boots in the switches are logged in the evlog.

## Battery energy

n/a: a stability test.
