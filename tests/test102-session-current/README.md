# test102: K230 session current

Status: **planned** (2026-10-21): not run yet.

Battery life = sleep current x time + session energy x wakes. The K230 rails' current at idle, during the change
net, the encoder and Wi-Fi.

## Setup

**At USB (the way it is done):** a multimeter in the VBUS wire of the **USB-C 1** cable, the only cable carrying power
(USB-C 2's VBUS cut, 2026-10-21; PR1 carries data only): one cable powers the board and flashes the K230
([usb_meter.png](usb_meter.png) shows the meter in a USB cable's VBUS). Consoles: both on PR1 (`/dev/ttyACM0` BL616,
`/dev/ttyACM1` K230, tests/test039); USB-C 2 is not needed. The cell must be charged (D1 off): then the USB current is
the board's own draw. Board power = that current x VBAT (C15 pad 1), not x 5 V (the linear charger's drop is not
paid on battery). A / 10 A input, 0.001 A. Rules (2026-10-21):
- **10 A jack and range, locked** (no autorange). The mA range's shunt + fuse (2-5 ohm) dropped VBUS 0.4-1 V at
  200 mA and reset the board on current steps; autorange switching opens the path briefly.
- **Meter in the red VBUS wire**, GND joined straight through: a meter in GND offsets the board's ground against the
  host and every USB device on the board (PR1, K230) fails.
- **Check the mA fuse** of a borrowed meter: with a burned fuse it reads nothing useful.
- **Only one cable may carry power**: both USB-C ports feed VUSB, so with two plugged in the meter reads a share.
- **Meter in after the port is up**: plug USB-C 1 with the meter leads shorted, wait until the board runs (D1 solid
  if charging), then open the short. With the meter in line at plug-in, the voltage sags at the port's switch-on
  and the charger's ~1 A start, and the charger only blinks D1 (no VIN-DPM in ILIM mode, bugs/v2.txt 9).
- "LEAd" on the GD118B = the red lead's jack does not match the function; it has auto power-off (APO) - disable it
  (key at switch-on, see the manual) for long runs.

**On battery (sleep current only):** with USB present the BL616 stays awake (USB mode, BLE; test firmware as product), so the
sleep current needs the meter in the cell's + lead, USB unplugged (test104).

![USB meter hookup](usb_meter.png)

## Needs (build features and dependencies)

**MISSING**: the kernel options / drivers, SDK components, libraries and programs this test uses (the union over all
tests is what the minimal image keeps).

## Steps

1. Rails off (BL616 only): sleep-level current (measured in test031: 35 uA on the battery, 2026-10-25).
2. `pt_on` held in reset, booting, agent idle, KPU loop, encoder (test027), LEDs at 100 % (white, IR).
3. Integrate over a typical session (test101) for the energy per wake.

## Pass

Recorded figures; energy per wake and the number of wakes a 1000 mAh cell allows.

## Result

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | **MISSING** | target (test022) | longest step: |
| This part's boot share | **MISSING** | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | **MISSING** | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test (test builds use the default flags: user, 2026-10-29) | the faster one stays | |

## Boot time

Boot energy per wake from test022's timing x its current.

## Battery energy

This test is the energy measurement of a session; its figures go into ENERGY.txt.
