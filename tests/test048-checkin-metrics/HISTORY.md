# test048-checkin-metrics: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-06 (run.py, 538 s): pass

33/33 checks pass.


## Obstacles

- 2026-11-06: the two BL616 images flashed one by one (`make flash` in each directory): both went to slot 0, slot 1
  kept an older setup image; the two setup images switched into each other every ~2.5 s with USB never up, so no
  `isp` from the console: BOOT link + EN by hand, then the pair in one run from bl616_full (`make flash
  SLOT1_BIN=../bl616_setup/build/build_out/leakcam_setup_bl616.bin`). Own mistake.
- 2026-11-06: the VM's USB controller stalls (`hub_ext_port_status failed`, err -32) at the BL616's re-enumeration
  after a reset: 1 in 10 plain `reboot`s, 0 in 5 full -> setup -> full round trips; not the slot switch. Cleared by
  the xHCI unbind/bind (board.py xhci_reset()).
- 2026-11-06: first look at the check-ins after a phone's wake on USB power: `sw 2` but `ks 0`, `klast -1` although a
  relay ran: the setup firmware's timed return (switch_to_full.c back_task) reset the chip with the K230 relay on,
  never through the session's end. Fixed: switch_now() ends the relay first (a4a09a9); this run: `ks 1`, `kms 120003`.
