# 3. K230 rails and USB boot

- **Mac on USB-C 1 gives no power with rails on** -> CH340X D+ back-feeds VUSB via D3 (~2.2 V), USB-C source
  waits for <0.8 V -> plug USB before `pt_on`; rev 2 fix. [bug-002](../../bugs/bug-002/README.md)
- **PG_1V8 pad too small** -> implied by 1V1 being up.
- **`pt_off` 12 ms vs 56-82 ms** -> noted, harmless.
