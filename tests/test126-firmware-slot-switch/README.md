# test126: the slot switch between the full and the setup firmware, both ways

Status: **pass, steps 1-7** (2026-11-07 07:50 UTC, 10 round trips on the product pair: full -> setup 4.7 / 8.0 / 8.1 s,
setup -> full 4.4 / 7.6 s (min / median / max; one 22.8 s with a VM USB controller reset), the table age +1 per switch,
the PIN and the stored network the same in both firmwares; 2026-11-02 first pass). The way into the setup firmware is
`switch_to_setup lost` (`wake` until the phone's wake was removed, 2026-11-07): a bare `slot switch` with USB power and a network stored is handed straight
back to the full firmware (test121/test127, by design). Design: DESIGN.md 5.3.1 (two firmwares, slot switch).

The low-level proof under every firmware change of DESIGN.md 5.3.1: each firmware can make the other one run, by
the command alone, with no trigger logic yet. The triggers come after it (USB plug -> full: test121 + this; presence
lost -> setup: test117; the 6 h retry: test125).

boot2 (v8.1.8) keeps two copies of the partition table (0xE000, 0xF000) and boots from the one with the higher age;
the FW entry has two images, slot 0 at 0x10000 (2 MB) and slot 1 at 0x210000 (1.41 MB; the SDK's unused mfg
partition overlaps it, nothing writes it). `slot switch` (firmware/bl616/slot.c) flips the FW entry's active index,
writes it to the other copy with age + 1 (bflb_boot2_update_ptable) and resets like `isp` (USB detached, 500 ms, POR
reset).

## Setup

- Slot 0: the full firmware (firmware/bl616_full). Slot 1: the setup firmware's test bed
  (tests/firmware/bl616_setup_test: the setup sources + test commands). Both with `slot`.
- Flashed together: `make -C firmware/bl616_full flash SLOT1_BIN=../../tests/firmware/bl616_setup_test/build/build_out/bl616_setup_test_bl616.bin`
  (host.mk: both images, the partition table into both copies).
- USB-C 1 in (both firmwares keep their USB console awake with USB power), the bench console on PR1.

## Needs (build features and dependencies)

BL616: boot2's partition table (bflb_boot2.c, partition.c, CONFIG_PARTITION), slot.c in both firmwares, the POR reset,
the USB console re-attaching after the reset.

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. After the pair flash: `slot` says active 0, the full firmware answers (`provision`).
2. Full -> setup: `slot switch` in the full firmware: the console back within a few seconds, the setup test bed
   answers (`lp_boot` in `help`), `slot` says active 1, age + 1. Time from the command to the new console.
3. Setup -> full: `slot switch` in the setup firmware: the full firmware back, active 0.
4. Each firmware's settings after the switch: easyflash shared (the PIN, the stored network's length and CRC, never the
   values), the crash record readable (`crash`) in both.
5. Repeat 2-3 ten times: every switch lands, the age counts up by one per switch, no boot loop, heap at start equal.
6. The boot2 retry byte: the FW entry's age top byte (PT_ENTRY_AGE_BOOT_RETRY_MASK) after each switch, whether boot2
   falls back by itself when a firmware never marks its boot.
7. Re-flash after switches: a plain `make flash` of one firmware boots it (both table copies written).
8. The trigger after this test (its own test): the setup firmware asleep in BLE sleep, USB plugged (test121's wake),
   the stored network checked up to the API (DESIGN.md 5.3.1), only then slot_switch(); a failed check stays in setup.

## Pass

Both directions switch every time from the command, within seconds, with the settings shared and no boot loop; a
re-flash after switches boots the flashed image.

## Result

- 2026-11-02 18:01-18:02 UTC, `run.py 10` again after the day's changes (the full firmware without BLE, `provision`, the
  watchdog set up again after every PDS wake, the modules moved; full and setup test bed 0.6+g1d2b375-dirty-202611021742):
  **PASS**, 20 switches, 4.3 s each (one 4.5 s), table ages 0 -> 20, PIN and network shared, no crash record. The full
  firmware's heap at its start 236828 B (210052 B right after the flash), 64 KB more than with BLE (172152 B).

- 2026-11-02 05:48-05:52 UTC, `python3 run.py 10` (full firmware 07:45 build in slot 0, the setup test bed 07:46 build
  in slot 1, flashed together; USB-C 1 in): **PASS, 20 switches**:
  - every `slot switch` landed in the other firmware: full -> setup 4.3 s every time, setup -> full 4.3 or 7.6 s
    (command to the next firmware answering `slot`, VMware's ~4 s USB attach included). The 7.6 s ones are one
    `slot` query (3 s) lost in the full firmware's Wi-Fi start logs (heap 145 k: its check-in had run), not a slower
    boot.
  - the partition table's age +1 per switch (0 -> 22), the FW entry's age with it; **boot2's retry byte (age top
    byte) stayed 0**: boot2 v8.1.8 counts no boot retries and never fell back by itself.
  - settings shared: the PIN stored and the stored network's length + CRC identical in both firmwares every time.
  - heap at start: full 172152 B before its check-in, 145 k after; setup test bed 169428 B after `lp_boot`: steady.
- Step 7: switched to setup (age 23), then `make flash` of the full firmware alone: the full firmware booted, both
  table copies at age 0, the setup image in slot 1 kept; both switches worked again after it.
- Step 4, the crash record: the same HBN RAM address in every image (0x20010c00, `rec`, the only .hbn_noinit
  member); `crash test` in the full firmware, then the switch: the setup firmware printed the full firmware's record.
  **But a record does not say which image wrote it**, and the backtrace decoded with the other image's ELF named wrong
  code (full: cmd_crash, crash_rec.c:249; the same addresses in the setup test bed: cmd_ble, pt_ble.c:115). Fixed:
  the record names its image (LEAKCAM_IMAGE from each CMakeLists.txt, "CRA2" magic) and `crash` says which ELF to
  decode with when it is the other one. To re-check on the board with the next builds.

## Metrics

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the BL616 alone, K230 off | target (test022) | |
| This part's boot share | n/a: no K230 boot | - | |
| Test run time | 3 min 0 s (run.py 10, 20 switches), no hand step | - | |
| switch time (command -> other firmware's console) | full -> setup 4.3 s, setup -> full 4.3 s (7.6 s with a lost query) | ~4 s of it VMware's USB attach | |
| Current idle / during (min / avg / peak) | n/a: on USB power, a switch is a reset (~1 s) | - | |
| Voltage under load (min) | n/a: on USB power | - | |
| Power, energy per operation | one 4 KB flash erase + write + reset per switch | - | |
| Per day | 8 switches a day at most (6 h retry, DESIGN.md 5.3.1) | - | |
| Temperature die / board (start / peak / end) | n/a: no load | - | |
| Memory BL616 / K230 (start / min / end), leak | full 172152 B at start (145 k after its check-in), setup test bed 169428 B after lp_boot, equal every round: no leak across switches | no leak | |
| Image (sizes) | full 1272144 B, setup test bed 1284496 B (slot 1 holds 1474560 B) | slot sizes | |
| Compile flags | BL616 -Os -g (SDK default), no LTO | smallest code (rule) | |
| Features over the minimal image | slot.c in both | - | |
| Optimised build (-O3, LTO, -march) | n/a | - | |
