# test122: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Superseded (2026-11-07, DESIGN.md 5.3.8, 5.3.9)

The battery reading moved from the first product's WAKE ACK (in `leakcam_agent`) to the `;b=<mV>` attribute on every
K230 frame of `/app/leakcam`. The old lines:

    K230: ADC_1 VBAT in the agent, the WAKE ACK reading, the quick stop (session end on a low reading).

    1. Low level, the K230's quick stop: a session running (capture or upload); the reading below the cutoff -> the agent
       ends the session ...

## Runs

None: not run yet.

## Obstacles

None.
