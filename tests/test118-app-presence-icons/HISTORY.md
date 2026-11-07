# test118: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.12)

The server's global `poll` interval is gone: the runner sets the board's check-in interval per device with `PUT
/v1/devices/<id>/intervals` (product default 3600 s, the bench server 60 s). The old line (Setup):

      board would), poll interval 10 s for the run.

## Runs

### Automated run 2026-11-01 (run.py, 217 s): pass

7/7 checks pass.

- phone USB port was the power source: swapped to sink


## Obstacles
