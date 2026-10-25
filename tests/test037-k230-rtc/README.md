# test037: K230 PMU RTC: start, set from the BL616, timekeeping

Status: **step 0 pass** (2026-10-21): RTC runs from Y1 (+25 ppm vs the 27 MHz timer), UTC fixed; handover, roll-overs, absolute ppm open.

The K230's PMU RTC (Y1, 32.768 kHz, AVDD1P8_RTC on the switched 1V8 rail) restarts at every power-up; the BL616
hands it the time in the `WAKE` reply and the K230 sets the RTC from it (DESIGN.md 5.4); NTP time goes back as
`TIME,<unix s>`. It needs the PMU awake (bug-003: R45 bridged, INT4 1.8 V); `RT_USING_RTC_PMU`.

## Setup

**MISSING**: hookup, firmware / image, and the probes named in Metrics.

## Needs (build features and dependencies)

K230 PMU RTC (RT_USING_RTC, RT_USING_RTC_PMU), musl time (UTC patch), rtc_raw/rtc_ppm (leakcam_bench.c); BL616 link for the handover.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

0. **Hardware configuration** (before any timing):
   - image: `CONFIG_RT_USING_RTC=y`, `CONFIG_RT_USING_RTC_PMU=y` in the built rtconfig.h; the PMU pads in the pin
     table (72-pad table, bug-003) as in the defconfig comments;
   - board: Y1 32.768 kHz with its load capacitors as in the schematic (values vs the crystal's CL), INT4 at 1.8 V
     (R45 bridged, bug-003), AVDD1P8_RTC on the 1V8 rail at 1.8 V with the rails on;
   - driver: the `rtc` device registered (`list_device`), its clock source the external crystal, not an internal RC
     (driver / PMU register, read back and printed);
   - oscillation: the RTC seconds advance between two reads 10 s apart (and, if a scope is at hand, Y1 at 32.768 kHz
     on the crystal pin, a 10x probe only).
   **Raw read:** `date` in msh, and the PMU RTC registers read directly (a temporary MSH command in the SDK copy):
   seconds, date fields, the valid / running flags, before and after a set.
1. Boot: `rtc driver register OK`; the time the RTC starts with before the handover (expect the reset value), and
   the time to the first valid read.
2. Handover: the `WAKE` time against the host's UTC; the RTC (`date` in msh) right after: offset <= 1 s; with
   `WAKE` 0 (BL616 clock lost) the RTC stays marked invalid and nothing stamps a clip with it.
3. Crystal Y1: RTC against the host over a 10 min session (the longest session): ppm; again at 0 and 50 C (test026).
4. Roll-overs: set 23:59:58 31 Dec and read through; set 2038-01-19 03:14:06 (32-bit overflow).
5. `TIME,<unix s>` back to the BL616 after NTP: the BL616's clock re-based (test032 drift starts from it).
6. Rails off and on 10 times: every boot starts the RTC (PMU wake), no hang (bug-003 regression).

## Pass

RTC within 1 s of the BL616 after every handover, Y1 within +-20 ppm, roll-overs correct, 10/10 cold starts.

## Results

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

### Automated run 2026-10-25 (run.py, 236 s): pass

3 boots: rtc_ppm 60 = +27.0, +27.0, +27.0 ppm. rtc_raw:

    rtc_raw
    rtc_raw: date 14190101 time 03000002 alarm_date 00010101 alarm_time 00000000 count 7fff391b int_ctrl 00000002
    rtc_raw: 2025-01-01 00:00:02 week 3 leap 0, curr_count 14619 sum_count 32767

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | pt_run -> msh 0.64 / 0.64 s (median / max of 3; SPL 0.16, OpenSBI 0.24, RT-Smart 0.25, mounted 0.63, msh 0.64); longest step to mounted 0.38 s (2026-10-25, run.py) | target (test022) | longest step: |
| This part's boot share | rtc driver registers at 0.255 s in the boot, no wait of its own (console milestones) (2026-10-25) | - | |
| Test run time | 236 s automated (run.py, 2026-10-25), no setup | - | setup (flashing, plugging, waiting) separately |
| RTC offset after handover, Y1 ppm | Y1 +27.0, +27.0, +27.0 ppm vs the 27 MHz rdtime (rtc_ppm 60, 3 boots); offset after handover not measured (needs an NTP reference) (2026-10-25) | <= 1 s, +-20 ppm | % of the top |
| Current idle / during (min / avg / peak) | msh idle 0.220 A (test006, 2026-10-25); the RTC's own share sits in the 35 uA sleep floor (test031) (2026-10-25, from recorded readings) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | VBAT 4.36 V min (K230 ADC ch 1, test023 calibration, 3 samples; on USB: the charger rail, not the cell) (2026-10-25) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | n/a: the RTC runs on the PMU always-on domain; its share is in the sleep floor (test031) (2026-10-25) | - | scripts/energy.py |
| Per day | n/a: as energy (2026-10-25) | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die 43.4 / 44.9 / 44.9 C; board 33.7 / 35.0 / 35.0 C (AHT20) (2026-10-25) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 208608 / min 204396 / 208608 B, blocks 14 -> 14, deleted 0; K230 heap available 7311744 B steady; leak: n/a, 3 snapshots (mem_leak.py needs >= 8 cycles) (2026-10-25) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | kdimg 45.4 MB (/sdcard 32 MiB, /bin 8 MiB one ISP set), kernel 3.75 MB, /bin 0.68 MB, /sdcard programs 6.76 MB | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os, -ffunction/-fdata-sections, --gc-sections, strip (no LTO) | smallest code (rule) | |
| Features over the minimal image | kernel test commands in leakcam_bench.o: 11.2 KB code + 0.6 KB RAM for all 13 (ddr/nand/uart_bench, boot_times, plt_*, rtc_*, pad_read, bat_watch); no init, 0 ms boot; not in the product image (2026-10-25) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | n/a: hardware-bound (crystal frequencies): compiler flags do not change the result (2026-10-25) | the faster one stays | |

## Boot time

PMU wake + RTC driver register, and the handover to the first valid read: part of test022's breakdown.

## Battery energy

n/a on its own (inside every K230 session); a slow RTC start would show in the boot time.

## Obstacles

- **msh `date` 8 h ahead of the RTC** -> RT-Thread's musl `sys/time.h` defines RT_LIBC_USING_LIGHT_TZ_DST with a +8 h
  default (time.c) -> install.sh sets it to 0 (UTC, user rule); the RTC driver itself was UTC (timegm/gmtime).
- **Flash chain stopped silently** -> the USB boot device came after my 4 s check (VMware click) -> wait up to 60 s
  for 29f1:0230 before flashing. (Since 2026-10-24: no click, the device attaches in ~2 s; the runners wait 20 s.)

None in the run of 2026-10-25.
