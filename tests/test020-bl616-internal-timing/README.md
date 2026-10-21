# test020: BL616: wake latency, flash storage, sensor reads

Status: **planned** (2026-10-21).

The always-on side's speed: how fast a wake turns into a K230 power-on, and the cost of its own jobs.

## Steps

1. Wake to K230_PWR high: from an RTC wake, a leak (ACOMP) edge and a USB plug (product firmware; a GPIO toggled at
   each step, scope or the K230's rdtime).
2. AHT20 measurement: trigger to data (datasheet 80 ms; the firmware reads at 50 ms then every 5 ms).
3. Easyflash/littlefs: write and read of a key (Wi-Fi credentials, BLE bonds) on the BL616's flash.
4. Leak probe ADC conversion time.

## Pass

Figures recorded; wake to K230 power-on within the design's budget (DESIGN.md).


## Boot time

The BL616 side of every wake: wake -> decision -> K230_PWR high (adds to every session before the K230 boot starts).

## Battery energy

The BL616's current while awake (humidity slice, wake handling) and how long; energy per humidity slice (144 per day) and per wake decision.
