# test026: NAND / UFFS power-loss robustness

Status: **planned** (2026-10-21).

The product writes clips, logs and references to /sdcard (UFFS on NAND) while the battery can run out or the BL616
can cut the rails. UFFS is log-structured; this proves it on rev 1.

## Steps

1. A test hook writes files in a loop (sizes 1 KB .. 2 MB, fsync, verify the previous file).
2. BL616 test firmware cuts the rails at random moments (`pt_off` after a random 2-20 s, `pt_on`, `pt_run`), 100
   times, console recorded.
3. After each boot: mount OK, the files written before the last fsync intact, free space not shrinking.

## Pass

100 random power cuts: every mount succeeds, no fsynced file lost or corrupt, no space leak.
