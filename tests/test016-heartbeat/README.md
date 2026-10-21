# test016: K230 heartbeat to the BL616 (K230_ALIVE)

Status: **planned** (2026-10-21).

The agent toggles K230 GPIO2 every 500 ms (net K230_ALIVE -> BL616 IO01, R32 100 k to 3V3); the BL616 ends a
session when the edges stop for 2 s. Without it a product session never ends (or ends early).

## Steps

1. BL616 test firmware: `pt_on`, `pt_run`; K230 boots the image with the agent.
2. BL616 `pt_mon on`: K230_ALIVE must change every ~0.5 s while the agent runs.
3. Stop the agent (or let the hook end): edges stop, the level stays.
4. `pt_off`: K230_ALIVE low, no back-feed (3V3 sense at 0 of 5).

## Pass

Edges at 1 Hz +-10 % while the agent runs, none after it stops; low with the rails off.
