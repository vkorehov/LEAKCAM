# test030: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

No runs yet.

## Obstacles

- **2026-11-11 21:50-22:12, the colour image could not be flashed: the K230 ROM never came on USB (~1 h).** My
  k230_usb_boot.py bug (the test firmware's "k230: command not found." read as a product refusal) plus USB-C 1 half
  plugged in; details, cases and the fix in test003 HISTORY.md (2026-11-11). Meanwhile I flashed the product BL616 pair
  to try its `k230 usbboot` and the test firmware back (two extra BL616 flashes).
- **2026-11-11 22:25, a 10 min recording does not fit /sdcard (14.8 MB free).** `leakcam_stream -f` now saves the
  first 4 MB per camera and keeps counting frames and gaps after (rebuild + flash).
- **2026-11-11 evening, "I2C answers, no frames" on J5 and earlier on J4 (hardware).** After one good J5 picture, every
  capture got no frames while the sensor answered on I2C, also after power cycles and before the agent's boot capture
  (software ruled out: registers = the driver's table, same image streams elsewhere). Cause found by the user: J5's
  connector plastic broke, the latch no longer clamps; the data contacts lose pressure first. The earlier J4 cases
  (fisheye no frames, stopgap fixed by a reseat) look the same: the fisheye's "faulty unit" verdict is open again.

- **2026-11-11, the K230 restarted every ~35 s under cam_shot (own mistake).** The test image was flashed while the
  BL616 still ran the product full firmware, which runs product K230 sessions and ends them without the product
  program's answers. The test image pairs with tests/firmware/bl616_powertest (`make flash` there, then
  `pt_on` + `pt_run`): the K230 then stays on. Restore the product pair afterwards (bl616_full `make flash
  SLOT1_BIN=...`, the product K230 image).
- **2026-11-11, 21 stray bytes in the first picture's base64 (test side).** Other console writers put lines in the
  middle; shot.py keeps whole base64 lines only and saves the raw output (shot_console.txt).

None recorded.
