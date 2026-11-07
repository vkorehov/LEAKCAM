# test106: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-07 (run.py, 623 s): pass

29/29 checks pass. First setup (app opened -> Done) 23.3 s; repeats 23.1-26.1 s.


### Automated run 2026-11-07 (run.py, 609 s): **FAIL**

26/28 checks pass. First setup (app opened -> Done) 23.5 s; repeats 22.7-26.5 s. Failed: save without checking: said so (26.5 s); save without checking: the new network stored.


### Automated run 2026-11-07 (run.py, 363 s): **FAIL**

4/17 checks pass. First setup (app opened -> Done) 20.5 s; repeats 20.1-21.3 s. Failed: first setup: Done (20.5 s, pairing ); first setup: the network stored; wrong password named (21.1 s); wrong password: the old network kept; network not seen named (20.3 s); network not seen: Save without checking offered; network not seen: the old network kept; save without checking: said so (20.3 s); save without checking: the new network stored; change back: Done (20.9 s); change back: the first network stored again; 8 setups in a row: every one Done (0/8); after the reset: Done with no Forget by hand (20.3 s, pairing ).


### Automated run 2026-11-07 (run.py, 441 s): pass

29/29 checks pass. First setup (app opened -> Done) 10.0 s; repeats 10.2-13.5 s.


### Automated run 2026-11-04 (run.py, 293 s): pass

12/12 checks pass (on battery).


### Automated run 2026-11-04 (run.py, 3 s): **FAIL**

not run: not the full firmware with the network.


### Automated run 2026-11-02 (run.py, 356 s): **FAIL** (false: memory verdict)

29/29 checks pass. First setup (app opened -> Done) 8.5 s; repeats 8.4-9.5 s. Final firmware pair (2 s advertising
asleep, 100 ms awake, board Security Request) and the app bonding with every connect; no dialog. The FAIL was the leak
verdict over the repeats' samples: each setup ends in the switch back (a reset), the samples were fresh boots of the
full firmware (194308-210052 B by their timing, the last three equal); run.py takes no repeat samples on the product
pair since.


### Setup speed on the setup firmware, 2026-11-02 20:30-20:56 UTC (traces, 15 + 15 + 4 + 1 setups)

Board console and phone logcat timestamped beside the app's steps (one setup each, credentials masked).
- Send -> Done 5.9 s -> 4.3 s with two fixes: the setup firmware's lwipopts_user.h lacked the full firmware's
  `DHCP_DOES_ARP_CHECK 0` (1.0 s from the DHCP ACK to the address in use); advertising every 100 ms while awake (USB
  power, a phone's wake) instead of 2 s (connect 1.6 -> 1.1 s). App opened -> Done 8.5-9.9 s, 15/15, no pairing dialog.
- Left: the board's scan for the network 0.6 s (could run while the phone pairs); pairing + writes 1.8 s (phone side).
- Slow setups (19-31 s, the system "Pair" dialog) reproduced only with 2 s advertising (2 of 6): the phone's direct
  connect missed adverts and gave up after its 5 s limit (status 133), twice; the app's early `createBond()` failed
  with it (SMP_FAIL 101); the third connect paired through the board's Security Request, which Samsung confirms with
  its dialog. A debug build logged a board-side connect only when the link came up: the phone never reached it
  before. At 2 s even good connects took 2-3 s.
- No board Security Request (tried, 21:04 UTC): every pairing refused, "pairing without the PIN refused": Android's
  own request has no MITM flag. Kept. Fix in the app instead: `createBond()` with every connect, retries too (the
  dialog comes when the board's request arrives with none pending): 10/10 at 2 s without a dialog, two slow ones
  (16, 23 s). User: 2 s kept on battery (1 s tried in the source, reverted before any flash).

### Automated run 2026-11-02 (run.py, 457 s): pass

29/29 checks pass. First setup (app opened -> Done) 10.3 s; repeats 12.5-23.5 s.


### Automated run 2026-11-01 (run.py, 216 s): pass

17/17 checks pass. First setup (app opened -> Done) 8.5 s; repeats 8.2-9.1 s.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 216 s): pass

17/17 checks pass. First setup (app opened -> Done) 9.1 s; repeats 8.3-9.2 s.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 211 s): pass

17/17 checks pass. First setup (app opened -> Done) 7.9 s; repeats 7.8-8.4 s.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 227 s): **FAIL**

16/17 checks pass. First setup (app opened -> Done) 8.9 s; repeats 8.9-15.6 s. Failed: 8 setups in a row: every one Done (7/8).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 374 s): **FAIL**

16/17 checks pass. First setup (app opened -> Done) 8.9 s; repeats 8.7-160.9 s. Failed: 8 setups in a row: every one Done (7/8).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 306 s): **FAIL**

14/17 checks pass. First setup (app opened -> Done) 10.0 s; repeats 8.6-15.6 s. Failed: network not seen named (0.0 s); network not seen: Save without checking offered; 8 setups in a row: every one Done (7/8).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 231 s): pass

17/17 checks pass. First setup (app opened -> Done) 9.8 s; repeats 9.4-11.1 s.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 242 s): **FAIL**

16/17 checks pass. First setup (app opened -> Done) 12.2 s; repeats 9.5-10.5 s. Failed: BLE started again after a reset.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 322 s): **FAIL**

16/17 checks pass. First setup (app opened -> Done) 16.6 s; repeats 10.5-18.4 s. Failed: 8 setups in a row: every one Done (7/8).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 6 s): **FAIL**

not run: no pt_ble in the BL616 firmware.


### Automated run 2026-11-01 (run.py, 214 s): pass

17/17 checks pass. First setup (app opened -> Done) 7.5 s; repeats 6.9-7.6 s.

- phone USB port was the power source: swapped to sink


### Product v1 with STATUS reconciliation 2026-11-01 07:33 (run.py, 250 s): pass

17/17 checks pass on the product image (firmware/bl616) with the reconciliation change: the board sets STATUS idle on
connect and JOINING inside the commit write, the app reads STATUS every second besides the notifications (one
idempotent handler, the timeout counts from a state change). First setup (app opened -> Done) 7.9 s; repeats 7.1-7.6 s
(no slower than notifications alone); BLE again after a reset, 7.3 s; crash record none before and after.

- phone USB port was the power source: swapped to sink (it came back as source after the USB drop below)


### Product v1 2026-11-01 (firmware/bl616, Gitea PR #1): pass with one unexplained stall

- run.py on the product image (07:09-07:13 UTC+2, stopped by hand at the stall): crash record none before; steps 1 3 4 5
  6 pass - first setup 12.9 s (pairing included), wrong password 12.1 s, network not seen 11.3 s, save without
  checking 15.2 s, change back 7.3 s; repeats 7.9, 7.5 s, then **repeat 3: 67.6 s "The LEAKCAM did not answer"**
  after "Password accepted. Getting an address from the router..."; the next setup 8.5 s Done. Board alive (mem
  sampled right after: free 154548, unchanged), no crash record, no reset.
- repeat_capture.py 20 (new: the BL616 console held open the whole run, every line timestamped,
  repeat_capture.log): 20/20 Done, 7.0-8.5 s; mem free 154548-154552, min 128268, blocks 103 throughout (no leak);
  crash record none. Each check: join 0.3 s, DHCP ack 20 ms, `check of "VK": state 0x03` about 0.4 s after the join
  started.

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

- --battery needs the product pair in the full firmware with the real network stored (2026-11-04, automated run)

- memory leak (mem_leak.py verdict in the Memory row): a leak fails the test (2026-11-02, automated run)

- 2026-11-02 20:19 UTC: the first run on the product pair stopped after repeat 1: the VM's xHCI hung on the board's
  USB drop at the firmware switch (`hub_ext_port_status err -32`), port gone for 3 min; a rebind of 0000:0a:00.0
  brought it back, the board was fine (full firmware, no crash record). Re-run with a watcher that rebinds on the
  error (scratchpad, not needed again in that run). The VM also restarted at 20:07 UTC and left the board on the
  Mac's side until a replug.
- 2026-11-02: the phone on adb over Wi-Fi from now on (no free USB-C port); the runner reconnects from
  `~/.leakcam_phone`. A trace script's logcat pipe filled (read only at the end) and logcat stalled after 1.7 s:
  logcat goes to a file.
- 2026-11-01, use case 2: setups 17 s (from 7 s): the app's LEAKCAM list above the steps pushed the fields down and
  the driver scrolled for them (one repeat never found them). The board's check took 1.4 s and the app showed Done
  7.4 s after opening. Fixed: the list below the setup; 9.4-10.5 s since, the driver re-finding redrawn fields.
- 2026-11-01: product detection and the BLE-on check read the console while the presence check-in's Wi-Fi logs flooded
  it: both now wait for their own answer and retry once.

- the BL616 test firmware on the board has no pt_ble: flash tests/firmware/bl616_powertest (2026-11-01, automated run)

- 2026-11-01, product v1: one setup of 24 got no answer after the ADDRESS progress step (the app's 60 s step timeout);
  not reproduced in 20 captured repeats. The board sent each STATUS change once as a notification (bt_gatt_notify's
  result unchecked, no resend) and the app learned the result only from notifications: one lost notification left it
  waiting with the board done. Fixed by design, not by a detector (user: reconciliation and eventual consistency):
  STATUS is the state, idle on connect, JOINING set in the commit write, and the app reads it every second as well;
  a lost notification now only delays the answer by up to a second. Rule in skill leakcam-product-firmware section 3.
- 2026-11-01 07:19:55: the board and the phone dropped off USB together, a minute after repeat_capture.py ended (no VM
  pause in the vmware log); the user reconnected both. The phone came back as the USB power source (run.py swapped it).
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

## Step 7 by hand: 2026-10-31

hand_step7.py (test040 hand_steps.py helpers; their dispatch now under `__main__` so they import): 5/5 checks pass.
Obstacle: the bench phone was not on USB at the start (not in lsusb, nothing in dmesg); the user plugged it in, adb
saw it as a power sink, unlocked.
