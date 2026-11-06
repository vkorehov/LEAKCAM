# test046: the K230's battery reading on every link frame, kept by the BL616 (low level: the link, both firmwares)

Status: **planned** (2026-11-06): built (K230 image, BL616 pair), not run.

The K230 is the only one that can read the battery (VBAT/3 on ADC_1, tests/test023). The product program reads it at
its start and attaches it to every frame it sends as `;b=<mV>` (k230_link.h). The BL616 takes it from every frame and
keeps it in HBN RAM (bl616_common/battery.c) for the phone and the server (DESIGN.md 5.3.9). On USB power the ADC
reads the charger's rail, not the cell: the BL616 must not keep that reading.

## Setup

Product K230 image and BL616 product pair, bench cable on USB-C 1, BL616 console on PR1. For the battery steps:
USB-C 1 out by hand (the cell in), the console stays on PR1.

## Steps

1. On USB power, a K230 session (the setup session: `provision erase`, reboot): the K230 log shows
   `battery <mV> mV: t <ms>`; the frames on the link carry `;b=`; `battery` on the BL616 says "charging (USB power)"
   and keeps the old value (none after a battery change: -1).
2. On battery (hand: USB-C 1 out), a K230 session: `battery` shows the K230's reading (within +-30 mV of the K230's
   own log line) and "on battery".
3. The value survives: a BL616 reset (`reboot`), the switch to the other firmware (`switch_to_setup lost`, then back)
   and a sleep: `battery` the same in each.
4. Every frame: a K230 command and its ACK both carry `;b=` (the BL616's `link rx:` lines in the powertest firmware,
   or the K230 console's frame dump); a frame without it still parses (the old K230 image).
5. A broken reading: `;b=-1` (the ADC failed) changes nothing kept.
6. Timing: the battery read adds <= 20 ms before READY (K230 log `t` of `start` -> `battery`).
7. **The periodic STATUS** (every 60 s, k230_link.h): in a session held 3 min the BL616 logs `k230: STATUS <mode>,<s>,
   <version>,<die x10>,<heap>,<heap min>,<pages>, battery <mV> mV kept` each minute, each frame with a fresh `;b=` (on
   battery: the kept value follows the K230's). The other fields reach the check-in: tests/test048 step 11.

## Pass

Every K230 frame carries the reading; the BL616 keeps the last one taken on battery, across resets, the slot switch
and sleep; a USB-power reading is never kept; READY no later than 20 ms more than before.

## Needs (build features and dependencies)

K230: the ADC driver (`/dev/adc`, CONFIG_RT_USING_ADC), the product program's battery read. BL616: battery.c in both
firmwares, `.hbn_noinit` sorted by name (bl616_leakcam.ld.in).
