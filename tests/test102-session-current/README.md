# test102: K230 session current

Status: **planned** (2026-10-21).

Battery life = sleep current x time + session energy x wakes. The K230 rails' current at idle, during the change
net, the encoder and Wi-Fi.

## Setup

**At USB (the usual way):** a multimeter in the VBUS wire of the USB-C 2 (CH340) cable, the only cable carrying
power ([usb_meter.png](usb_meter.png)): USB-C 1 unplugged (it feeds VUSB directly; USB-C 2 feeds it through D2), PR1
data only. The cell must be charged (D1 off): then the USB current is the board's own draw. Board power = that
current x VBAT (C15 pad 1), not x 5 V (the linear charger's drop is not paid on battery). A / 10 A input, 0.001 A.

**On battery (sleep current only):** with USB present the product BL616 firmware stays awake (USB mode, BLE), so the
sleep current needs the meter in the cell's + lead, USB unplugged (test104).

![USB meter hookup](usb_meter.png)

## Steps

1. Rails off (BL616 only): sleep-level current (the BL616 test firmware does not hibernate: product firmware later).
2. `pt_on` held in reset, booting, agent idle, KPU loop, encoder (test026), LEDs at 100 % (white, IR).
3. Integrate over a typical session (test101) for the energy per wake.

## Pass

Recorded figures; energy per wake and the number of wakes a 1000 mAh cell allows.


## Boot time

Boot energy per wake from test021's timing x its current.

## Battery energy

This test is the energy measurement of a session; its figures go into ENERGY.txt.
