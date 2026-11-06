# test044: the phone sees the board over a USB-C cable (low level: the USB link the app will use)

Status: **steps 1-6 pass** (2026-11-05): the phone (USB host, powering the cable) lists the board's USB device 1.41 s
after the K230 starts (median of 5, max 1.58 s), 3 s faster than the bench through VMware; step 7 waits for the app's
USB code.

The setup channel's first choice is the phone app over a USB-C cable, the phone the USB host. This test proves the
link from the phone's side: plugged into USB-C 1 (the K230's USB0), the phone lists the board's USB device after
every K230 start, as host, powering the cable. The board's side was proven against the bench in test043; this one
also times it without VMware's ~3.3 s.

## Setup

- K230 test image with the USB device (CherryUSB CDC ACM, 1209:ABD1; tests/firmware/test_install.sh), BL616 test
  firmware (K230 power and console over PR1, which stays on the bench).
- Phone on adb over Wi-Fi (its USB port is the one in use), unlocked by the runner.
- A USB-C to USB-C cable phone <-> USB-C 1. Hand steps through ~/.leakcam_hand: `phone` once plugged in, `bench`
  once USB-C 1 is back on the bench cable.

## Needs (build features and dependencies)

The K230 USB device (test043); nothing over the minimal image beyond it. Later steps: the app's USB host code
(UsbManager, permission), the USB setup relay (test113).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. Hand: USB-C 1 from the bench to the phone.
2. K230 off: the phone lists no board device.
3. 5 K230 starts: the phone lists 1209:ABD1 after each; time from pt_run (median, max).
4. The phone's port: power role source, data role host (the phone powers the board's USB side).
5. K230 off: the device leaves the phone's list; time.
6. Hand: USB-C 1 back to the bench.
7. Later (the app's USB code): the app shows the USB link icon for this board, only while the link is up (test129's
   rule); a plug alone stays the plug icon.

## Pass

The phone lists the board's USB device after every K230 start, as host, and drops it when the K230 is off.

## Result

2026-11-05, run.py, 4/4 pass (151 s with the two hand steps): K230 off -> nothing listed; 5/5 starts listed after
1.38-1.58 s (median 1.41 s; the phone polled over adb on Wi-Fi every ~0.2 s, so up to ~0.2 s of it is the poll); port
power_role source, data_role host; gone from the list 0.28 s after pt_off.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the K230 console is not read; the phone's listing time is the measure | target (test022) | |
| Test run time | 151 s automated (run.py, 2026-11-05), no setup | - | |
| Phone sees the K230 USB device (median / max after pt_run), gone after pt_off | phone sees the K230 USB device 1.41 s (median of 5), max 1.58 s after pt_run; gone 0.28 s after pt_off | test043: K230 USB up 0.73 s | |
| Current idle / during (min / avg / peak) | n/a: not metered in this run | floor (test031) | |
| Voltage under load (min) | n/a: on the phone's USB power | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: setup only | - | |
| Per day | n/a: setup only | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: short starts | K230 limit (datasheet) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: no K230 console in this run | no leak | |
| Image (sizes) | BL616 test firmware 1322048 B (2026-11-05, on the board at the run) | minimal image (rule) | |
| Compile flags | K230 kernel -O1 -g, sections, --gc-sections (the test image) | smallest code (rule) | |
| Features over the minimal image | CherryUSB device + CDC ACM (test image only) | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |

## Boot time

The phone's listing time after pt_run, step 3.

## Battery energy

n/a: setup only, on the phone's USB power.
