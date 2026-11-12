# test142: the leak thresholds from the server, kept in flash (whole product flow, product firmware)

Status: **not run yet** (written 2026-11-10).

DESIGN.md 5.3.16 (user, 2026-11-10): the probe's comparator reference (mV) and the humidity alarm (%RH) are the
server's, per device (`PUT /v1/devices/<id>/thresholds`), and come as attributes of every check-in (presence) answer
(`"probe_mv"`, `"humid"`). The board keeps them in flash (thresholds.c, easyflash `thr`), writes only when they differ
from the stored ones, uses them at once (ACOMP1 re-armed, the humidity compare) and reports the ones in use (`thmv`,
`thrh`). Defaults until the server ever sent any: 1100 mV, 85 %RH.

## Setup

Product BL616 pair + product K230 image, the full firmware with a network stored, on USB power at the bench (no
hand), the server on the Pi. Check-ins triggered by `presence now`; the humidity simulated (`climate sim`).

## Steps

1. **Defaults:** PUT 0/0; after a check-in the board is on 1100 mV / 85 %RH and reports them (`thmv`, `thrh`).
2. **New values:** PUT 900 mV / 70 %RH; at the next check-in `thresholds` shows them "from the server", one flash write,
   `leak` shows the comparator re-armed at 900 mV; the next check-in reports them.
3. **Written only when different:** two more check-ins with the same values: no flash write.
4. **Kept in flash:** a reset with the server down: 900 mV / 70 %RH in use, the probe armed at 900 mV.
5. **The alarm at the server's value:** 75 %RH alarms at 70 (would not at 85); the server opens the leak
   `humidity high`, judged at the device's threshold.
6. **The setup firmware:** the same values from the same flash; back to the full firmware.
7. **Back to the defaults:** PUT 0/0: 1100 mV / 85 %RH, the probe re-armed.

## Pass

The thresholds follow the server per device through the check-in answer, survive a reset without the server and the
firmware switch, are written to flash once per change and never for the same values, and both alarms use them.

## Result

Not run yet.

## Needs (build features and dependencies)

BL616: bl616_common/thresholds.c (both firmwares), presence.c (the answer's attributes), leak_check.c
(leak_check_probe), climate.c (the humidity compare), metrics.c (`thmv`, `thrh`). Server: thresholds.go part of
main.go (PUT, thresholds.json, the answer, leakIn per device), MetricKeys thmv/thrh.

## Metrics (skill leakcam-test-metrics)

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, reset release -> app init | n/a: test021 | test021 | |
| This part's boot share | n/a | - | |
| Test run time | MISSING | - | |
| Thresholds taken | MISSING | at the next check-in | |
| Current idle / during | n/a: no meter, one flash write per change | floor (test031) | |
| Memory BL616 / K230 (start / min / end), leak | n/a: no repeated allocation (a static blob) | no leak | |
| Image (sizes) | n/a | minimal image (rule) | |
| Speed / rate against the top | n/a | SPEEDS.txt | |
| Voltage under load (min) | n/a: no meter | cutoff 3.10 V (bug-004) | |
| Power, energy per operation | n/a: no meter | - | |
| Per day | n/a: no meter | ENERGY.txt line | |
| Temperature die / board (start / peak / end) | n/a: test140 | K230 limit (datasheet) | |
| Compile flags | BL616 -Os -g | smallest code (rule) | |
| Features over the minimal image | thresholds.c | test008 floor | |
| Optimised build (-O3, LTO, -march) | n/a: not an optimisation test | the faster one stays | |
