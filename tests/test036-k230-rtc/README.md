# test036: K230 PMU RTC: start, set from the BL616, timekeeping

Status: **planned** (2026-10-21).

The K230's PMU RTC (Y1, 32.768 kHz, AVDD1P8_RTC on the switched 1V8 rail) restarts at every power-up; the BL616
hands it the time in the `WAKE` reply and the K230 sets the RTC from it (DESIGN.md 5.4); NTP time goes back as
`TIME,<unix s>`. It needs the PMU awake (bug-003: R45 bridged, INT4 1.8 V); `RT_USING_RTC_PMU`.

## Steps

1. Boot: `rtc driver register OK`; the time the RTC starts with before the handover (expect the reset value), and
   the time to the first valid read.
2. Handover: the `WAKE` time against the host's UTC; the RTC (`date` in msh) right after: offset <= 1 s; with
   `WAKE` 0 (BL616 clock lost) the RTC stays marked invalid and nothing stamps a clip with it.
3. Crystal Y1: RTC against the host over a 10 min session (the longest session): ppm; again at 0 and 50 C (test025).
4. Roll-overs: set 23:59:58 31 Dec and read through; set 2038-01-19 03:14:06 (32-bit overflow).
5. `TIME,<unix s>` back to the BL616 after NTP: the BL616's clock re-based (test031 drift starts from it).
6. Rails off and on 10 times: every boot starts the RTC (PMU wake), no hang (bug-003 regression).

## Pass

RTC within 1 s of the BL616 after every handover, Y1 within +-20 ppm, roll-overs correct, 10/10 cold starts.

## Metrics (to fill on every run: skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | not measured yet | target (test021) | longest step: |
| This part's boot share | not measured yet | - | |
| Test run time | not measured yet | - | setup (flashing, plugging, waiting) separately |
| RTC offset after handover, Y1 ppm | not measured yet | <= 1 s, +-20 ppm | % of the top |
| Current idle / during (min / avg / peak) | not measured yet | floor (test030) | USB-C 2 VBUS meter, D1 off; VBAT at C15 |
| Power, energy per operation | not measured yet | - | scripts/energy.py |
| Per day | not measured yet | ENERGY.txt line | n per day |

## Boot time

PMU wake + RTC driver register, and the handover to the first valid read: part of test021's breakdown.

## Battery energy

n/a on its own (inside every K230 session); a slow RTC start would show in the boot time.
