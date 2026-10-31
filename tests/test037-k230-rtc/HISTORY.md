# test037: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-31 (run.py, 236 s): pass

3 boots: rtc_ppm 60 = +27.0, +27.0, +27.0 ppm. rtc_raw:

    rtc_raw
    rtc_raw: date 14190101 time 03000002 alarm_date 00010101 alarm_time 00000000 count 7fff2bea int_ctrl 00000002
    rtc_raw: 2025-01-01 00:00:02 week 3 leap 0, curr_count 11242 sum_count 32767


### Automated run 2026-10-25 (run.py, 236 s): pass

3 boots: rtc_ppm 60 = +27.0, +27.0, +27.0 ppm. rtc_raw:

    rtc_raw
    rtc_raw: date 14190101 time 03000002 alarm_date 00010101 alarm_time 00000000 count 7fff391b int_ctrl 00000002
    rtc_raw: 2025-01-01 00:00:02 week 3 leap 0, curr_count 14619 sum_count 32767

### 2026-10-21, bench image (rtc_raw, rtc_ppm), NAND 80 MHz

| Check | Result |
|---|---|
| Config: RT_USING_RTC / RT_USING_RTC_PMU | in the image (defconfig, rtconfig.h); `rtc driver register OK` at boot |
| Start value | 2025-01-01 00:00:00 at power-up (reset value), counts from there: 00:00:23 at ~23 s |
| Raw registers (`rtc_raw`) | date 14190101, time 03000017, count curr 5614 / sum 32767, int_ctrl 2 |
| Oscillator | sub-second counter 32768 per s: **+25 ppm against the K230's 27 MHz timer** (30 s, twice, equal) -> Y1 crystal, not an RC (that would be % off) |
| Absolute ppm | not yet: against host NTP the console latency (40 ms) gives +-330 ppm over 120 s; needs >= 20 min |
| `date` | was 8 h ahead (RT-Thread musl light time zone, default +8 h); **install.sh patch: UTC** -> `date` = RTC |

Steps 1-6 (handover from the BL616, roll-overs, 10 cold starts) still to do: the handover needs the product BL616
firmware.

### Superseded statements

- Status: **step 0 pass** (2026-10-21): RTC runs from Y1 (+25 ppm vs the 27 MHz timer), UTC fixed; handover, roll-overs, absolute ppm open.

## Obstacles

(Both bullets from 2026-10-21; order kept.)

- **msh `date` 8 h ahead of the RTC** -> RT-Thread's musl `sys/time.h` defines RT_LIBC_USING_LIGHT_TZ_DST with a +8 h
  default (time.c) -> install.sh sets it to 0 (UTC, user rule); the RTC driver itself was UTC (timegm/gmtime).
- **Flash chain stopped silently** -> the USB boot device came after my 4 s check (VMware click) -> wait up to 60 s
  for 29f1:0230 before flashing. (Since 2026-10-24: no click, the device attaches in ~2 s; the runners wait 20 s.)
