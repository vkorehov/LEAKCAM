# test106: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-10-30 (run.py, 249 s): pass

17/17 checks pass. First setup (app opened -> Done) 8.0 s; repeats 7.3-9.4 s.

- phone USB port was the power source: swapped to sink


### Automated run 2026-10-30 09:29 (run.py, 258 s): **FAIL** (op app; board and app passed what was reached)

13/17 checks pass. First setup (app opened -> Done) 7.3 s; repeats 0.0-14.6 s. Failed: save without checking: said so (11.3 s); save without checking: the new network stored; 10 setups in a row: every one Done (6/10); after the reset: Done with no Forget by hand (0.0 s, pairing None).

- phone USB port was the power source: swapped to sink
- With the setup-speed firmware and app: wrong password named (fw status 33), not seen named with Save without
  checking offered, every completed setup 7.3-7.7 s. Failures were op app's: three StaleObjectExceptions (each left
  the board's BLE link half open, so the next Find saw nothing and the one after took 14 s) and a tap on Save without
  checking lost during the scroll to it. op app retries both since. README metrics of this run reverted.


### Automated run 2026-10-30 08:24 (run.py, 665 s): **void** (runner bugs; two real findings)

9/17 checks pass. First setup (app opened -> Done) 28.6 s; repeats 14.2-89.7 s. Failed: BLE advertising on USB power; wrong password named (28.8 s); network not seen: Save without checking offered; save without checking: said so (0.0 s); save without checking: the new network stored; change back: Done (14.3 s); after the reset: Done with no Forget by hand (0.0 s, pairing None); 10 setups in a row: every one Done (8/10).

- phone USB port was the power source: swapped to sink
- Void: most failures were run.py / BleOp bugs (see Obstacles). Real: a wrong passphrase showed as "router did not let
  the LEAKCAM join (code 0)", and 2 of 10 repeats waited 20 s for an address the router had given (both fixed in the
  firmware the same day, see README Result). Its README metrics were reverted.

No run yet. Status before the step 6 rewrite: **planned** (2026-10-27).

## Obstacles

- The 08:24 run's runner (2026-10-30), each fixed in run.py / BleOp:
  - "BLE advertising" failed: the check looked for "already on", `pt_ble` says "pt_ble: on".
  - "memory leak" was false: a watchdog reset just before the repeats made the first memory sample a fresh heap
    (157 KB free against a steady 131 KB); the reset step now comes after the repeats.
  - "Save without checking not offered": the button sits below a long message in the ScrollView, outside UI
    Automator's tree until scrolled to; op app scrolls now.
  - setups returning after 0.0 s: a StaleObjectException when the pairing dialog closed between finding and pressing
    it ended op app and left Samsung's dialog open for 30 s (the user saw it hang); caught now.
  - `pressBack()` meant for the keyboard closed the app (no keyboard opens when UI Automator sets the text).
- The Wi-Fi passphrase on the phone: `am instrument -e psk <hex>` is logged by adbd into logcat; it was there through
  test040 and I printed it while reading logcat (2026-10-30). Arguments now go in a file pushed and removed by
  phone.py; the phone's log buffers were cleared.
- The user used the phone during a timeline run: the app was not in front, Find saw nothing (2026-10-30). Hands off
  the phone during runs, as for the board.
- The setup app never got the bond broadcast with a NOT_EXPORTED receiver (Samsung, Android 13): it sat on "pairing"
  until its 60 s timeout although the bond was made. Fixed: registered exported (2026-10-30).
