# test126: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

- 2026-11-02 05:48 UTC, `run.py 1`: one round trip, PASS (4.3 s each way): the runner's first check.

## Obstacles

- **2026-11-07, three things since the first pass:** (1) a bare `slot switch` to the setup firmware came back to the
  full firmware at once (table age +2): the setup firmware hands a board with USB power and a network stored to the
  full firmware (test121/test127), so the runner goes in with `switch_to_setup wake` (2 min hold); (2) the settings
  read in slot 1 still used the old test bed's `lp_boot` / `pt_ble creds`: both firmwares answer `provision` now,
  asked again when the first command got no answer; (3) `which()` once said setup in slot 0 (a garbled read): it now
  takes the image and the active slot from one answer and asks again when they disagree. The VM's USB controller
  stopped twice in 20 switches (VMware): the runner resets it after 15 s without the port, logged.

- 2026-11-02 17:58-18:00 UTC, before the regression run: the VM's USB controller hung twice in one pair flash (after
  `isp`, then after the flashed firmware's start: `hub_ext_port_status failed (err = -32)` without end), the third and
  fourth time that evening, each right after a board disconnect, all with USB-C 1 (the K230's USB) plugged back in.
  Rebinding the xHCI brought the ROM, then the console; the flash itself had passed. The run then had a watcher
  (rebinds on new err -32 lines); it never fired in the 20 switches. The first command after the attach came back
  "slot: command not found" (garbled), the repeat answered.

- The test bed opens easyflash only in `lp_boot`: `make flash`'s PIN step (provision_pin.py) failed on it before
  `lp_boot` ("no pairing PIN state"), though the PIN was stored. Flashed as slot 1 behind the full firmware it does
  not matter (the full firmware answers `prov`); flashing the test bed alone, run `lp_boot` and ignore that step.
- `make flash` used to write the partition table at 0xE000 only; after a switch the copy at 0xF000 is newer and still
  names the other slot, so a re-flash would have booted the old image. host.mk now writes both copies (step 7 proves
  it).
- The crash record names no image: see README Result, step 4 (fixed in crash_rec.c).
