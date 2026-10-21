# test026: K230 die temperature (TS)

Status: **planned** (2026-10-21).

`CONFIG_RT_USING_TS=y`: the K230's die temperature sensor. How hot the K230 gets in a session, and with the
enclosure closed.

## Steps

1. Read the TS device at idle right after boot, then every 10 s for 10 min.
2. Same under load: KPU change net in a loop, then the encoder (test027), both cameras when they arrive.
3. Same with the board in the closed enclosure.

## Pass

Readings plausible (idle a few C above room temperature, monotonic warm-up), and under worst-case load well below
the K230's limit (datasheet); note the margin for the enclosure.
