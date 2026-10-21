# test102: K230 session current

Status: **planned** (2026-10-21).

Battery life = sleep current x time + session energy x wakes. The K230 rails' current at idle, during the change
net, the encoder and Wi-Fi.

## Setup

**At USB (the usual way):** a multimeter in the VBUS wire of the USB-C 2 (CH340) cable, the only cable carrying
power ([usb_meter.png](usb_meter.png)): USB-C 1 unplugged (it feeds VUSB directly; USB-C 2 feeds it through D2), PR1
data only. The cell must be charged (D1 off): then the USB current is the board's own draw. Board power = that
current x VBAT (C15 pad 1), not x 5 V (the linear charger's drop is not paid on battery). A / 10 A input, 0.001 A.

**Used 2026-10-21 (tests 015/016/021):** VBUS of the USB-C 2 cable cut instead, the meter in USB-C 1's VBUS: one
cable then powers the board and flashes the K230, and the CH340 still works (U2 runs from the switched 3V3). Rules
learned that day:
- **10 A jack and range, locked** (no autorange). The mA range's shunt + fuse (2-5 ohm) dropped VBUS 0.4-1 V at
  200 mA and reset the board on current steps; autorange switching opens the path briefly.
- **Meter in the red VBUS wire**, GND joined straight through: a meter in GND offsets the board's ground against the
  host and every USB device on the board (PR1, CH340, K230) fails.
- **Check the mA fuse** of a borrowed meter: one with a burned fuse read nothing useful; shorting its leads "fixed"
  the board. Readings from before the swap are void.
- **Only one cable may carry power**: both USB-C ports feed VUSB, so with two plugged in the meter reads a share.
- **Meter in after the port is up**: plug USB-C 1 with the meter leads shorted, wait until the board runs (D1 solid
  if charging), then open the short. With the meter in line at plug-in, the voltage sags at the port's switch-on
  and the charger's ~1 A start, and the charger only blinks D1 (no VIN-DPM in ILIM mode, bugs/v2.txt 9).
- "LEAd" on the GD118B = the red lead's jack does not match the function; it has auto power-off (APO) - disable it
  (key at switch-on, see the manual) for long runs.

**On battery (sleep current only):** with USB present the product BL616 firmware stays awake (USB mode, BLE), so the
sleep current needs the meter in the cell's + lead, USB unplugged (test104).

![USB meter hookup](usb_meter.png)

## Steps

1. Rails off (BL616 only): sleep-level current (the BL616 test firmware does not hibernate: product firmware later).
2. `pt_on` held in reset, booting, agent idle, KPU loop, encoder (test026), LEDs at 100 % (white, IR).
3. Integrate over a typical session (test101) for the energy per wake.

## Pass

Recorded figures; energy per wake and the number of wakes a 1000 mAh cell allows.


## Metrics (to fill on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| Speed / rate (none for this test) | not measured yet | - | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | to fill | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | not measured yet | K230 limit (datasheet) | chip_temp, AHT20 |

## Boot time

Boot energy per wake from test021's timing x its current.

## Battery energy

This test is the energy measurement of a session; its figures go into ENERGY.txt.
