# test026: H.265 encoder with synthetic frames

Status: **planned** (2026-10-21).

The heaviest job of a leak session: two H.265 streams, 1280x960, 15 fps, ~500 kbit/s each (DESIGN.md). The
encoder can be fed synthetic frames, so speed and memory can be measured before the cameras arrive.

## Steps

1. A test program (SDK copy) feeding the MPP VENC from a VB pool with generated frames (moving pattern), one channel,
   then two, at the product's settings; write the stream to /sdcard.
2. Measure encode time per frame, CPU load, memory (VB pools, heap), the stream's bit rate and its IDR spacing.
3. Play the stream back on the PC (ffprobe/ffplay) to check it decodes.

## Pass

Both channels hold 15 fps with margin, memory leaves room for the change net and the network stack, the stream
decodes and keeps the rate and IDR interval.


## Boot time

Encoder / media stack init time (VB pools, VENC) at boot or at first use.

## Battery energy

Battery current with one and two encoder channels running; energy per second of clip (two cameras, 15 fps).
