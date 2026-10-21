# test022: K230 session current

Status: **planned** (2026-10-21).

Battery life = sleep current x time + session energy x wakes. The K230 rails' current at idle, during the change
net, the encoder and Wi-Fi.

## Setup

A meter or USB power meter in the battery lead (board on battery only), or a shunt; the BL616 test firmware.

## Steps

1. Rails off (BL616 only): sleep-level current (the BL616 test firmware does not hibernate: product firmware later).
2. `pt_on` held in reset, booting, agent idle, KPU loop, encoder (test019), LEDs at 100 % (white, IR).
3. Integrate over a typical session (test020) for the energy per wake.

## Pass

Recorded figures; energy per wake and the number of wakes a 1000 mAh cell allows.
