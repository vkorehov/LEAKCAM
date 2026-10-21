# test017: DDR stress (64 MB at 2667 MT/s)

Status: **planned** (2026-10-21).

SPL trains the DDR at 2667 MT/s on every boot (test006). A marginal setting shows only as rare random crashes.
This test runs a pattern memory test over most of the free RAM for a long time.

## Steps

1. A temporary memtest command (SDK copy only, as the test014 bench): allocate the largest free block, write and
   verify walking ones/zeros, address-in-address and random patterns, report errors with address and pattern.
2. Run it in a loop for >= 1 h, the console recorded (skill k230-rtsmart-hangs, `console_record.py`).
3. Repeat with the KPU and the encoder busy (heat, power noise) if the first run is clean.
4. Repeat after a few cold starts (each start retrains).

## Pass

0 errors over >= 1 h and >= 5 cold starts; no reset (a silent reset = watchdog: the test must sleep between passes).
