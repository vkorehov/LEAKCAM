# test121: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

## Obstacles

- 2026-11-01: the SDK board file's `io_test` example configures pulls on every pin group (IO0-15 pull-up, IO20-34
  pull-down) and three wake pins: not for this board (IO0 holds the K230 in reset). Own config: only IO3 unmasked,
  falling edge, no pulls added in sleep (PGOOD has its own 100 k).
- The IO wake callback runs in the low-power layer's software interrupt (bl616_lp_callback.c): it only notifies a
  task, which ends the sleep window (USB console resume needs task context).
