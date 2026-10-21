# test015: K230 DDR bandwidth

Status: **planned** (2026-10-21).

DDR runs at 2667 MT/s (SPL training, test006), 64 MB. Bandwidth decides how much the KPU, the encoder and the
camera pipeline can do at once.

## Steps

1. A temporary MSH command (SDK copy, as the test014 bench; waits sleep, skill k230-rtsmart-hangs): `memcpy`,
   read-only (sum) and write-only (`memset`) over 1, 4, 16 MB blocks, cached; timed with `rdtime` (27 MHz).
2. Same through the uncached alias (DMA-visible), as the KPU/VENC see it.
3. Same while the KPU runs the change net in a loop (test012) and while the encoder runs (test027).

## Pass

Recorded MB/s per case; the drop under concurrent load; no errors (compare with test025's stress).
