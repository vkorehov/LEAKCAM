# test013: SDIO wiring, K230 MMC0 <-> BL616 SDIO, 2026-10-19

The Wi-Fi link's six lines (BL616 IO13 CLK, IO12 CMD, IO14/15/10/11 D0-D3, 10 k pull-ups to the switched 3V3) without
the BL616's SDIO device firmware. BL616: `bl616_powertest` `pt_sdio`; K230: a temporary reader of its SD host's
present-state register (MMC0 at 0x91580000, offset 0x24: D0-D3 in bits 20-23, CMD in bit 24; via `/dev/mem`) and the
NetHub driver's probe (`/dev/bl616` ioctl 0x4c01).

| Step | Result | |
|---|---|---|
| Idle, nobody driving | CMD and D0-D3 high (present state 0x03ff00f0) | pass |
| BL616 drives CMD/D0-D3: all high, each one low, each one high, all low (12 patterns), K230 reads | every pattern read back exactly: each line connected, none stuck, no two shorted | pass |
| K230 probes MMC0 (rescan), BL616 counts edges for 8 s | CLK 4.78 M edges (undercounted at 2.1 M samples/s), CMD 126, D0-D3 0 (no data phase without a card) | pass |
| Probe result | "no card" after 35 ms: expected, the BL616 runs the test firmware, not its SDIO device | |

**Pass.** Next: the BL616 SDIO device firmware, enumeration, Wi-Fi, and the bus speed raised to the fastest clock
that runs without CRC errors.



## Boot time

n/a: a wiring check.

## Battery energy

n/a: a wiring check.

## Obstacles

- **K230 MMC0 pads are dedicated, not GPIO** (no `pinRead`) -> the SD host's present-state register shows the live
  line levels.
- **The BL616 cannot count a 400 kHz-and-up clock exactly** by polling (2.1 M samples/s) -> edges only prove the line
  toggles; speed comes with the real link.
