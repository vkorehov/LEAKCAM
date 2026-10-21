# test032: Whole leak pipeline, end to end

Status: **later** (excluded for now, user 2026-10-21): needs the cameras, Wi-Fi and the product BL616 firmware.

Every part measured on its own (KPU 0.5 ms, SDIO, NAND, encoder): the time from a wake to the server's answer,
with everything running together.

## Steps

1. Wake -> K230 READY (test027) -> capture both cameras -> change net -> history -> POST /v1/check -> answer ->
   (leak) RTMP clip start: timestamps at each step on the K230 console (console_record.py).
2. Same with the server answering "leak" (the clip path) and "no leak" (back to sleep).
3. Battery cost: with test029's current, the energy per wake for each path.

## Pass

Recorded timeline; the longest step named; the leak-to-video time within what the user expects.
