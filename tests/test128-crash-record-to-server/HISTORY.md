# test128: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

- 2026-11-02 07:04-07:10 UTC: PASS 1 2 3 5 7 (README Result).
- 2026-11-02 06:47 UTC, `run.py 1`: step 1 PASS (the record at the server 4.1 s after the console was back).
- 2026-11-02 06:51-06:55 UTC, `run.py "2 3 5 7"`: 4 FAIL, all from the runner and the server (below), none from the
  firmware: repeated by hand afterwards, 5 watchdog hangs and 3 exceptions in the full firmware each came back in the
  full firmware with the partition table unchanged, and each record reached the server.

## Obstacles

- 2026-11-02 07:06:38 UTC, the VM's USB controller hung (`hub_ext_port_status failed (err = -32)` without end, 3 s
  after the board's disconnect for a crash reset): the BL616 console vanished, step 5's switch got no port and the
  runner stopped. Rebinding the VM's xHCI (0000:0a:00.0) brought it back (memory vmware-usb-connect-prompt); the
  board itself was fine and had sent its record at its next check-in.
- 2026-11-02 ~07:10 UTC, the USB-C 1 meter switched itself off (auto-off), which cuts USB-C 1's 5 V; the user
  reconnected it. The board read USB power yes before and after.
- **The server kept crash records in memory** (as its presence): the runner stops and starts it (steps 3, 7), and the
  records were gone; the runner's before/after counts compared a fresh list with an old one. Worse for the product:
  the board clears its record on the server's answer, so a server restart lost it for good. Fixed: one JSON line per
  record in ~/leakcam-server/crashes.jsonl (LEAKCAM_CRASHES), synced before the answer, read back at the start; a
  write error answers 500 and the board keeps the record.
- **(build, count) is not a key:** the count starts at 1 again after every send, so two separate crashes of one build
  share it and the server dropped the second as a duplicate. Fixed: a resend is the same record text; that is the key.
- **My runner switched the firmware away:** `which()` returned None when the full firmware's Wi-Fi logs drowned both
  answers, and `if which() != "full": switch()` took that for "in setup" and ran `slot switch` in the full firmware
  (partition-table age 4 after 1-2 intended switches; step 3's record came from the setup test bed). Fixed: which()
  asks up to 5 times, runners switch only on a definite "setup" (tests/test126 run.py).
- The device ID read from the USB serial number is empty while the board resets: the server answered 400 to the
  runner's query and the run stopped. Read once at the start now.
- Step 5 as first written could not happen: the full firmware checks in 5 s after its start and sent the record before
  the runner switched. The step now keeps the server stopped across the crash and the switch.
- The board's HTTP client was GET only (wifi_check.c `internet()`, a few lines on an lwIP socket; lwIP's httpc is GET
  only too): POST added for the record (a form body), user's question 2026-11-02.
