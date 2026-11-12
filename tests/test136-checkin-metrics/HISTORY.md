# test136-checkin-metrics: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-12 (run.py, 148 s): pass

36/36 checks pass.


### Automated run 2026-11-12 (run.py, 151 s): **FAIL**

35/36 checks pass. Failed: form size within the 512 B buffer (with ssid and version) (433 B + ~80).


### Automated run 2026-11-11 (run.py, 134 s): pass

36/36 checks pass.


### Automated run 2026-11-10 (run.py, 139 s): pass

36/36 checks pass.


### Automated run 2026-11-10 (run.py, 146 s): **FAIL**

35/36 checks pass. Failed: ... kago -1 (the session ended in another boot), ksago the full firmware's own STATUS (0-30 s) (ksago 5, kago 7).


### Automated run 2026-11-09 (run.py, 128 s): pass

35/35 checks pass.


### Automated run 2026-11-09 (run.py, 137 s): **FAIL**

34/35 checks pass. Failed: tdie 25-60 C (23.7 C).


### Automated run 2026-11-09 (run.py, 130 s): **FAIL**

32/35 checks pass. Failed: no check-in from the wake's setup firmware (); the full firmware's first check-in: sw 2, ks 2, klast 1 (both sessions ended by the switches) (sw 1 ks 0 klast 1 kms 8230 kon 0 kfail 0); the credential check-in (setup): ks 0 (the session still on) (ks 1).


### Automated run 2026-11-09 (run.py, 512 s): **FAIL**

31/35 checks pass. Failed: switch_to_setup lost and back on USB power (not back); the full firmware's first check-in: sw 2, ks 2, klast 1 (both sessions ended by the switches) (sw None ks None klast None kms None kon None kfail None); the one after: sw 0, ks 0 (none); the credential check-in (setup): ks 0 (the session still on) (ks 1).


### Automated run 2026-11-09 (run.py, 148 s): **FAIL**

32/35 checks pass. Failed: switch_to_setup wake: in the setup firmware (not in 30 s); the full firmware's first check-in: sw 2, ks 2, klast 1 (both sessions ended by the switches) (sw 0 ks 0 klast 1 kms 20780 kon 0 kfail 0); the credential check-in (setup): ks 0 (the session still on) (ks 1).


### Automated run 2026-11-09 (run.py, 176 s): **FAIL**

32/34 checks pass. Failed: the full firmware's first check-in: sw 2, ks 2, klast 1 (both sessions ended by the switches) (sw 0 ks 0 klast 1 kms 45194 kon 0 kfail 0); the credential check-in (setup): ks 0 (the session still on) (ks 1).


### Automated run 2026-11-07 (run.py, 244 s): pass

34/34 checks pass.


### Automated run 2026-11-07 (run.py, 463 s): pass

34/34 checks pass.


### Automated run 2026-11-07 (run.py, 575 s): pass

34/34 checks pass.


### Automated run 2026-11-07 (run.py, 584 s): pass

34/34 checks pass.


### Automated run 2026-11-07 (run.py, 561 s): **FAIL**

32/34 checks pass. Failed: ... K230 values plausible (kup 0 ktdie 557 kheap 6717376 kheapmin 6717376 kpages 46059520 ksago 6 kago -1); ... ages -1 after the switch (another BL616 boot) (ksago 6, kago -1).


### Automated run 2026-11-06 (run.py, 538 s): pass

33/33 checks pass.


## Obstacles

- **2026-11-12 03:05, CHECK FAIL "form size within the 512 B buffer": a product bug.** The metrics are 433 B now
  (thresholds and K230 fields since 2026-11-10); the prefix is "ssid=" + the network name url-encoded (32 B -> up to
  96) + the image's version url-encoded (up to 108): ~650 B at worst, and wifi_check.c called vAssertCalled() on an
  overflow - a board on a long or non-ASCII network name would have crashed at every check-in. The test's flat "+80"
  margin hid that worst case. Fixed: the form buffer 768 B, the request buffer 1024 B (wifi_check.c); the test checks
  the computed worst case against 768.

- **2026-11-07, not run at the start:** the precondition read "no network stored" on a board that was online: the
  first `provision` after the port opened went unanswered (the lost first command, also test113's erase). phone.creds()
  asks again until the answer is complete.

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

## 2026-11-11 00:04 UTC+2: one check out of date (fixed in the test)

"kago -1 (the session ended in another boot)": kago 7, ksago 5. Since 2026-11-10 the metrics' ages run on the RTC
through resets and firmware switches (HBN sleep work, DESIGN.md 5.3.10), so kago is the relay session's real age. The
check now wants 0-300 s.
