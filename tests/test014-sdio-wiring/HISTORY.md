# test014: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-11 (run.py, 213 s): pass

8 boots, pt_sdio watch during bl616_rescan: CLK 5557338-5566072 edges, CMD 124-126 edges, D0-D3 0/0/0/0 (last boot; no data phase without a card).


### Automated run 2026-10-25 (run.py, 210 s): pass

8 boots, pt_sdio watch during bl616_rescan: CLK 5593283-5594738 edges, CMD 126-126 edges, D0-D3 0/0/0/0 (last boot; no data phase without a card).

### Temperature (2026-10-19 run)

Not recorded (the test ran before the temperature rule, 2026-10-21). If re-run: K230 die (`chip_temp`) and board (AHT20) at start, peak and end.

### Run 2026-10-19 (Results)

| Step | Result | |
|---|---|---|
| Idle, nobody driving | CMD and D0-D3 high (present state 0x03ff00f0) | pass |
| BL616 drives CMD/D0-D3: all high, each one low, each one high, all low (12 patterns), K230 reads | every pattern read back exactly: each line connected, none stuck, no two shorted | pass |
| K230 probes MMC0 (rescan), BL616 counts edges for 8 s | CLK 4.78 M edges (undercounted at 2.1 M samples/s), CMD 126, D0-D3 0 (no data phase without a card) | pass |
| Probe result | "no card" after 35 ms: expected, the BL616 runs the test firmware, not its SDIO device | |

**Pass.** Next: the BL616 SDIO device firmware, enumeration, Wi-Fi, and the bus speed raised to the fastest clock
that runs without CRC errors.

## Obstacles

- **K230 MMC0 pads are dedicated, not GPIO** (no `pinRead`) -> the SD host's present-state register shows the live
  line levels.
- **The BL616 cannot count a 400 kHz-and-up clock exactly** by polling (2.1 M samples/s) -> edges only prove the line
  toggles; speed comes with the real link.
