# test010: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-25 (run.py, 243 s): pass

8 boots: 112 READY frames with valid checksums on the BL616, echo runs clean.

### First run 2026-10-18 (115200 8N1): pass

| Direction | How | Result | |
|---|---|---|---|
| K230 -> BL616 | BL616 `pt_uart on`, K230 reset: the agent sends `READY` | `$1,READY*FC` .. `$13,READY*0F`, checksums intact; each frame 5x (resent while no ACK comes) | pass |
| BL616 -> K230 | BL616 sends a line per second; K230 `cat /dev/uart1` | `LEAKCAM BL616 uart test 27` .. `40`, consecutive, none lost | pass |

**Pass.**

#### Temperature

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Former placeholders (2026-10-18 README)

- Setup: **MISSING**; the setup is in the introduction above and in the Results.
- Steps: **MISSING**; the steps are the rows of the Results tables.
- Pass: **MISSING**; every Results row carries its pass / fail.

## Obstacles

- **Each READY 5 times** -> by design: the agent resends while no ACK comes (the test firmware sends none).
- **`cat /dev/uart1` hangs the shell (no Ctrl-C)** -> reset the K230 from the BL616 (`pt_hold`, `pt_run`).
- **`killall leakcam_agent` did not stop it** -> still listed afterwards; it did not get in the way.
