# test040: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-04 (run.py, 379 s): pass

39 of 39 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).


### Automated run 2026-11-04 (run.py, 376 s): **FAIL**

38 of 39 checks passed; failed: no write succeeds after a cancelled pairing ([]) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).


### Earlier status and result (README before 2026-11-03)

Status: **pass** (2026-11-01): run.py 46/46 in 338 s on the test firmware with STATUS reconciliation (attempt id, kept
across connections, a passed check stored also with the phone gone, notified only on change), fail fast and checked
return values; hand steps 1 and 2 (2026-10-29); step 5's full power cut not done (user: the cell stays in).

To re-prove (user, 2026-11-02, DESIGN.md 5.3.1): the check's end goal is the API reachable: join -> address -> internet
-> a check-in answered by the presence server; STATUS gains "API not reachable"; and it runs on the setup firmware
(all setup there, the full firmware has no BLE). The pass above is on the earlier check (internet as the last step).

- Steps 3-9 pass, 46 of 46 checks (run.py runs 12 and 13, 2026-10-29): every write limit enforced; credentials
  stored exactly (no old tail bytes); kept over HBN and a watchdog reset; checked store reports JOINING -> ADDRESS ->
  INTERNET -> SAVED; a wrong passphrase fails and stores nothing (0x11 WRONG_PASSWORD since 2026-10-30, the SDK's
  firmware status read, test106; before it 0x12 JOIN_FAILED), unknown SSID 0x10 NOT_FOUND, the old credentials kept;
  link dropped during a check: nothing stored; a second commit starts no second check; BLE and Wi-Fi run together.
- Step 8 (BLE first, during a session; run 13, -O3 + LTO, 2026-10-29): the session is back 0.1 s after a check during
  it, 0.1 s after SAVED (checked and unchecked), 3.4 s after a failed join to a missing network, 0.1 s after SAVED with
  a changed router password.
- Step 9: 20 store cycles, 0 failed, BL616 `mem` flat: no leak (runs 12 and 13, 2026-10-29).
- Hand step 1 (2026-10-29): advertises on USB power; a held, paired connection drops 0.1 s after USB-C 1 out; no
  advertising on battery (15 s scan); advertising again after the plug (the test firmware needs `pt_ble on`, the
  product reboots into its USB mode). Hand step 2: no connection on battery, so no pairing and no write.
- BLE start -> advertising 0.28-0.29 s (radio init 0.187 s); the phone's scan saw it 2.1-6.0 s after BLE start (scan
  timing) (2026-10-29).
- No passphrase and no key dump on the console over 6 joins and leaves (measure_build.py, run 13 image, 2026-10-29);
  the SDK's psk / key-dump / LTK prints are removed by sdk_patch.py.
- PDS A/B decided (pds_ab.py, 2026-10-29): with the SDK's PDS reset at BLE start the BL616's USB device drops for good;
  with the empty `btblecontroller_software_pds_reset()` hook every BLE start kept USB. The empty hook stays (user).
- No BLE bonds kept (CONFIG_BT_SETTINGS off): after a reset or HBN the phone pairs again (user decision).
- Test firmware back to the default build flags (-Os -g, the SDK's Kconfig default, no LTO; -O3 / LTO only for
  optimisation tests, user 2026-10-29), flashed 2026-10-30: `pt_ble on` keeps USB and advertises; test106 ran 18/18
  on it. The -O2 / -O3 / LTO comparison in Metrics stands (the product defconfig uses -O3 + LTO).

### Automated run 2026-11-03 (run.py, 368 s): pass

37 of 37 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).


### Automated run 2026-11-03 (run.py, 379 s): **FAIL**

35 of 37 checks passed; failed: commit without an SSID refused (GATT None); offset (long) write refused (GATT None) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).


### Automated run 2026-11-03 (run.py, 398 s): **FAIL**

33 of 37 checks passed; failed: check, unknown SSID: 0x10 ([]); checked store: JOINING -> ADDRESS -> INTERNET -> SAVED ([]); a second commit during the check starts no second check (); 8 store cycles (1 failed) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).


### Automated run 2026-11-01 (run.py, 449 s): pass

49 of 49 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 445 s): pass

48 of 48 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 380 s): pass

46 of 46 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 380 s): pass

46 of 46 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 09:21 (run.py, 338 s): pass

46 of 46 checks passed, shortened (store cycles 20 -> 8, the dropped-link wait polled). Two runs before it failed on
this change, not the board: STATUS notified only on change suppressed a second SAVED with the same (0) attempt id
(fixed: each commit clears STATUS first); "nothing stored with the link dropped" was the old rule (the board now
stores; the phone reads STATUS). One earlier failure of 4/20 store cycles was the phone: writes never left it (pairing
dialog not answered in time, the laptop on its charger replug), store_cycles_capture.py: 19/20, the one the phone. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 486 s): **FAIL**

44 of 46 checks passed; failed: 20 store cycles (4 failed); the real network stored again at the end () Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 703 s): **FAIL**

42 of 46 checks passed; failed: store unchecked 6/8 bytes: SAVED ([]); store 0x02 during a session: SAVED ([]); password changed: the session restarts on the old one and fails (); 20 store cycles (20 failed) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 (run.py, 506 s): **FAIL**

41 of 46 checks passed; failed: check with the link dropped: nothing stored ([1, 1, 2, 3, 4]); checked store: JOINING -> ADDRESS -> INTERNET -> SAVED ([1, 1, 2, 3, 4]); a second commit during the check starts no second check (); checked store during a session: SAVED ([1, 1, 2, 3, 4]); password changed: the check of the new one during the retries passes ([1, 1, 2, 3, 4]) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink



### Status line before the 2026-10-29 cleanup

Status: **pass** (2026-10-29): automated run.py 46/46 (runs 12, 13) and the hand steps 1 and 2; step 5's
full power cut not done (user: the cell stays in). Test firmware only.

### 2026-10-29 PDS A/B

2026-10-29 PDS A/B (pds_ab.py, test firmware): with the SDK's PDS reset at BLE start (ble_pairing.c's empty hook
removed) cycle 1: one USB disconnect at `pt_ble on`, the console gone for good, no advertising -> EN by hand. With
the empty hook: every BLE start of runs 12 and 13 kept USB. Decided (user: clear enough): the empty hook stays.

### 2026-10-29 hand steps

2026-10-29 hand steps (hand_steps.py, test firmware, cell in): step 1 - advertises on USB power; a held, paired
connection dropped 0.1 s after USB-C 1 out; no advertising on battery (15 s scan); advertising again after the plug
(15.3 s incl. the script's watchdog reset and `pt_ble on`: the test firmware does not restart BLE by itself, the
product reboots into its USB mode). Step 2 - no connection on battery, so no pairing and no write (the board refuses
connections and pairing without USB power). Meter (hand_steps.py meter, cell full): see Metrics.

### 2026-10-29 run 13

2026-10-29 (run.py, run 13, test firmware -O3 + LTO with the console fix of sdk_patch.py, 1633088 B): **pass**, 46
of 46 checks. Step 8: the session back 0.1 s after a check, 0.1 s after SAVED (checked and unchecked), 3.4 s after a
failed join to a missing network, 0.1 s after SAVED with the changed password (run 12, -O2: 4.6 s). Step 9: 20
cycles, 0 failed; BL616 free 105340 -> 105336 B, min 94568, 170 blocks over the 20 cycles: no leak. The phone saw
the advertising 6.0 s after BLE start (run 12: 2.1 s; the scan's timing, BLE itself starts in 0.28 s).
measure_build.py on the same image: no passphrase and no key dump on the console over 6 joins and leaves.

### Automated run 2026-10-29 (run.py, 494 s): pass

46 of 46 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink

### 2026-10-29 07:12 run 12

2026-10-29 07:12 (run.py, run 12, same test firmware re-flashed, 464 s): **pass**, 46 of 46 checks, every step
from 3 to 9 in one run. Step 8: the session rejoins 0.1 s after a check during it; checked and unchecked stores
restart it on the new network 0.1 s after SAVED; the real network back 4.0 s after a failed join to a missing
network; router password changed: the new one checked during the auth retries [1, 2, 3, 4], the session back 4.6 s
after SAVED. Step 9: 20 store cycles, 0 failed; BL616 `mem` identical after every cycle (free 106052 B, min 97420,
169 blocks): no leak. Left: the hand steps 1, 2, 5 and the current metrics.

### Automated run 2026-10-29 (run.py, 464 s): pass

46 of 46 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink

### 2026-10-28 22:05 run 11

2026-10-28 22:05 (run.py, run 11, test firmware with wifi_link_sta_leave()): 44 of 44 checks passed up to repeat
cycle 7, then the board left USB (the user stopped for the day). Step 8 in full: a check during a session runs and
the session rejoins; checked and unchecked stores restart the session on the new network; the real network back
4.7 s after a failed join to a missing network; router password changed (session in auth retries), the new one
checked during the retries: JOINING -> ADDRESS -> INTERNET -> SAVED, the session back 4.7 s after SAVED. To finish:
the 20 repeats (leak verdict), the hand steps 1, 2, 5, the current metrics, the feature-cut and optimised builds.

### Automated run 2026-10-28 (run.py, 447 s): **FAIL**

42 of 43 checks passed; failed: the real network stored again after a failed join: the session restarts on it (-1.0 s) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink

### Automated run 2026-10-28 (run.py, 521 s): **FAIL**

42 of 43 checks passed; failed: the real network stored again: the session restarts on it (-1.0 s) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink

### Automated run 2026-10-28 (run.py, 36 s): **FAIL**

the board was never seen advertising.

- phone USB port was the power source: swapped to sink

### 2026-10-28 19:06 run 5

2026-10-28 19:06 (run.py, run 5, test firmware with ble_pairing.c and every fix above, phone SM-G715FN): **pass**,
36 of 36 checks. Pairing hands-free (both Samsung dialogs answered by BleOp), MTU 247; every write limit enforced;
stored exactly (32/63, then 6/8 bytes); kept over HBN and a watchdog reset; checked store: wrong passphrase
JOINING -> 0x11 WRONG_PASSWORD, unknown SSID -> 0x10 NOT_FOUND, old kept each time; link dropped during a check:
nothing stored; the real network JOINING -> ADDRESS -> INTERNET -> SAVED with the phone connected over BLE all the
time (BLE and Wi-Fi together); a second commit started no second check; BUSY while the bridge had the radio (since
replaced: BLE first, step 8); 20 store cycles, no leak (BL616 free 106104 -> 106100 B). Earlier partial run
(08:44, phone and board left the VM's USB at 08:49) superseded by it. Runs 6-8 (19:29-19:41) void: run 6 was
never stopped and drove the board and phone under runs 7 and 8 (Obstacles).

### Automated run 2026-10-28 (run.py, 417 s): pass

36 of 36 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- bond after HBN sleep and wake: lost, paired again (CCC write GATT -1)
- bond after a watchdog reset: lost, paired again (CCC write GATT -1)

### Automated run 2026-10-28 (run.py, 129 s): **FAIL**

the board was never seen advertising.

### Automated run 2026-10-28 (run.py, 86 s): **FAIL**

the board was never seen advertising.

### Superseded metric values

- 2026-10-28: Memory BL616: free 175072 / min 97996 / 106100 B, blocks 19 -> 169, deleted 0; BL616 no leak
  (mem_leak.py, 21 snapshots).
- 2026-10-29: Test run time 464 s automated (run.py, run 12 with step 8), no setup.
- Compile flags (2026-10-28 / 2026-10-29): "BL616 test firmware -O2 (SDK default), no LTO (2026-10-28); since
  2026-10-29 product and test -O3 + LTO".
- Optimised build note (2026-10-29): "-O3 + LTO adopted in the product and test defconfigs".

## Obstacles

- store cycle 1: no write left the phone (connect None ms, pair None, bond None, error 'no connection'), retried (2026-11-04, automated run)

- store cycle 3: no write left the phone (pairing dialog not answered in time), retried (2026-11-03, automated run)
- store cycle 4: no write left the phone (pairing dialog not answered in time), retried (2026-11-03, automated run)
- store cycle 5: no write left the phone (pairing dialog not answered in time), retried (2026-11-03, automated run)
- store cycle 7: no write left the phone (pairing dialog not answered in time), retried (2026-11-03, automated run)
- store cycle 8: no write left the phone (pairing dialog not answered in time), retried (2026-11-03, automated run)

- 2026-11-01, first run after PR4: "BLE started (pt_ble on)" FAIL although the phone saw the board 7 s later: PR4
  replaced the firmware's "ble: advertising as" line with "ble: ready as" and the runners still waited for the old
  one. Fixed in every runner (test040, test042, test106, test110); run stopped and started again.

- 2026-10-30 12:20: the re-run after the audit found no BL616 port: board and phone both gone from the VM's USB
  (the hub drop of 2026-10-29 again); run.py read it as "no pt_ble" - not a test result.


- Step 8 at -O2 (run 12, 2026-10-29): the session back 4.0 s after the failed join, 4.6 s after the changed
  password (run 13 at -O3 + LTO: 3.4 s and 0.1 s; moved out of the README by the 2026-10-30 audit).

- Own mistake (2026-10-29, three times: hand_steps.py, pds_ab.py): runners started as `cd <dir> && nohup python3 ... &`
  - the `&&` chain is backgrounded as one shell, so `$!` (and the "stop" that used it) hit the shell, not Python; the
  first hand_steps run went on under the next one (void), pds_ab.py ran all 5 cycles after its "stop" (result
  unchanged: 0 of 5 cycles kept USB with the SDK's PDS reset) -> start runners with an absolute path, no `cd &&`
  (memory background-runner-pid).

- Board and phone left the VM's USB together again (2026-10-29 ~21:30, right after an EN press; both on hub 2-4)
  -> stopped for the day; the default-flag test firmware rebuild is not flashed yet.
- Board and phone left the VM's USB together (2026-10-29 ~09:00, after the LTO measurement, nothing running) -> the
  user reconnected them; both at once points to the VM / host USB, not the board.
- hand_steps.py watched the console port for the unplug, but the BL616 console runs over PR1, not USB-C 1: the
  port stayed -> the board's own reading (`pt_ble`: USB power yes/no). Then a watchdog-reset race (the old port name
  outlived the device: EIO) -> wait for the console to answer. Own mistake: killed the first run's shell wrapper, not
  its Python; it ran on under the next run, wrote into the same log and held the phone -> that run void, a clean
  run started alone (as runs 6-8 of 2026-10-28).
- The meter read 443 -> 370 -> 144 mA while the cell charged (the probes need a full cell) -> waited for the full
  charge; the user kept the cell in (taking it out is too complex and changes nothing for these readings), so the
  full power cut of step 5 was not done; persistence over reset, watchdog reset and HBN passed (runs 12, 13).
- Own mistake (2026-10-29): flashed the product firmware for steps 1 and 2 although the product is preliminary and
  only the test firmware is tested -> two BOOT + EN recoveries by the user; rule kept: test firmware only. What it
  showed: the product firmware does not boot (first product run since 7ccef2f: BLE, wifi_link, sdk_patch
  WRAM guard): after the flash no USB device, no BLE advertising, a USB replug changes nothing, 45-46 mA steady
  (awake, not in HBN); the same with -O2 (1284912 B) and -O3 + LTO (1608192 B), so not the flags. Its boot log goes to
  UART0 (the K230 link) and its evlog lives in HBN RAM, which the BOOT + EN recovery wipes -> each recovery needs BOOT
  + EN by hand. Left for the product rewrite.
- The key-dump fix took three tries: a filter on wifi_syslog's format missed (mm_sec_keydump() formats each row
  with snprintf and logs it as "%s"); -Wl,--wrap=mm_sec_keydump missed too (the caller is sm.c in the same prebuilt
  LTO library, resolved inside LTO; the fhost manager does not call it - own misreading of the map) -> wifi_syslog
  drops "%s" lines between the "Keyram Dump" and "MAC Address" headers (sdk_patch.py). measure_build.py checks the
  console for the passphrase and key rows on every run. The first wrap rebuild failed in 0.4 s: CMake re-ran for the changed SDK CMakeLists.txt inside the container without
  BL_SDK_BASE (own mistake in host.mk's incremental path) -> host.mk passes it.
- `pt_wifi join ... remembered` waits 20 s for DHCP on a rejoin with the same address although lwIP binds it at
  once (the got-IP event is missed): a test-command flaw, not the product's join (wifi_link.c); measure_build.py
  reports associate + WPA only for those joins.
- **The passphrase on the BL616 console** (2026-10-29, found while timing joins for the build comparison): every
  Wi-Fi join prints the supplicant's network config line by line, `Exec psk "<passphrase>"` among them, and every
  disconnect dumps the session keys ("Keyram Dump": PTK/GTK in hex). Product and test firmware both (the strings
  are in leakcam_bl616.elf). Sources: the psk line is an unconditional `printf("Exec %s")` in
  fhost_wpa_create_network (prebuilt libfhost_bl616_default.a, no switch); the key dump goes through wifi_syslog
  priority 4 (raw) from prebuilt libmacsw_bl616.a (LTO), the function itself is SDK source (macsw_plat.c).
  run.py never joined from the console with the real passphrase, so it did not see it; the pass criterion "never on
  a console" fails until fixed (sdk_patch.py, like the LTK prints of 2026-10-28).
- The phone powered the laptop again after a reconnect (2026-10-28 20:3x, user) -> the role swap holds only until
  the next USB reconnect -> tools/android-bench/phone_sink.sh re-applies it on every attach (run in the background;
  installed for good as the systemd user service phone-sink, 2026-10-28).
- Every BL616 build took over 10 min (2026-10-28) -> the SDK's project.build deletes CMakeCache.txt and CMakeFiles
  and regenerates autoconfig.h on each make, so the whole SDK recompiled -> fix: firmware/bl616/host.mk builds in
  build/ directly while defconfig, Kconfig and CMakeLists.txt are older than the cache (5 s unchanged, ~50 s for one
  file), the SDK's full path otherwise.
- A connect after a failed join never returned, the bridge's Wi-Fi task hung for good (2026-10-28, run 9 and 10:
  42 of 43, "the real network stored again" failed) -> how found: pt_trace counts 0x62 (connect called) one more
  than 0x63 (returned); reproduced on the console in a minute without the phone (pt_wifi store now goes through
  the same hook as a BLE store; pt_wifi link shows the supplicant state): after a join to a network that is not
  there, the supplicant kept scanning (wifi_mgmr_sta_state_get_ext() 3 = WPA_SCANNING) through autoconnect off and a
  disconnect, and a connect into that scan blocked; the next `pt_off` then hung the shell too (EN pressed twice).
  Steps: skip the disconnect only when DOWN (not enough); wait 3 s for idle (hung when the scan ran longer); wait
  20 s to measure: idle 2.6 s after the disconnect, but a second join started - our own leave's DISCONNECT came
  late and counted as a link loss; disconnect + 10 s wait: no hang, but 15 s after a changed password (a disconnect
  during an auth retry is lost) -> fix: wifi_link_sta_leave() disconnects every second until the supplicant is idle
  (max 10 s, else no connect), late own DISCONNECTs ignored for 5 s; the credential check uses it too (the user's
  case: the router password changed, the phone sends the new one while the board retries the old). Measured
  2.2-2.5 s after a missing network, 5.1 s after a changed password, no hang in 13 connects -> where:
  firmware/bl616/wifi_link.c, wifi_check.c, DESIGN.md 5.3, step 8 here.
- Popup unanswered for 15 s after each board reset (2026-10-28) -> run.py probed the old bond with a session that
  presses nothing; the board keeps no bonds (CONFIG_BT_SETTINGS off), so it asked to pair and the dialog waited out
  its timeout -> fix: run.py forgets the board on the phone and pairs again after a reset, no probe.
- Own mistake: runs 6, 7 and 8 overlapped (2026-10-28 19:29-19:41) -> how found: run 8 found `t040-rep7` stored (a
  repeat-cycle value run 8 never wrote) and the bridge's rails on; run 6's log went on to its repeats -> cause: run 6
  was started as `make flash && ... && nohup run.py &`, so `$!` was the shell of that chain, not the runner; the
  "stop" killed only that shell and run 6 ran on, the phone pairing popups of two runners crossed (the "hanging
  popup" the user saw), and run 7's failed scan, run 8's failed sessions and its "link dropped: stored" all came
  from it -> fix: run 6's runner stopped by its PID, the board reset by the watchdog, run.py takes a lock (a second
  runner exits at once); runs started with the runner itself as the background job -> runs 6-8 discarded.
- The phone powered the Mac instead of charging (2026-10-28 19:35) -> the user saw it; `dumpsys usb`: port0
  `power_role=source` (USB-C role negotiation picked the phone as source), battery down to 3 % -> fix:
  `dumpsys usb set-port-roles port0 sink device` (phone charging at +450 mA after it); run.py swaps the role at the
  start when it finds the phone as source.
- Run 6: pairing "no dialog", every write -1 (2026-10-28 19:30) -> how found: `dumpsys window` showed
  `isKeyguardShowing=true`; the pairing dialog opened over the lock screen and closed after 30 s, UI Automator
  cannot press through the lock screen -> fix: run.py stops at the start when the phone is locked; the bench phone
  needs its screen lock off (or Stay awake) -> the user unlocks it.
- Own mistake: a process listing printed the runner's `adb shell am instrument` command line, which carries the SSID
  and passphrase as hex (`-e writes ssid:...,psk:...`) (2026-10-28) -> only in the local terminal, nothing committed
  or sent; the passphrase was the wrong one of step 7, one character from the real one -> rule: never list the
  runner's child processes with their arguments; the values are visible to any local user through /proc while a
  call runs (bench only: the product app never puts them on a command line).
- The phone dropped off USB during the APK install (2026-10-28 19:05) -> the test APK install failed, the phone came
  back as a new USB device seconds later -> install repeated (`adb install -r -g -t`), passed.
- "Writes refused without pairing" passed on -1 (never delivered) as if refused (2026-10-28, own mistake) -> run.py
  counts only a real GATT error (> 0) as a refusal; after a cancelled pairing the link drops, so that check is now
  "no write succeeds" (!= 0) plus "nothing stored".
- Phone and board left the VM at 08:49 (2026-10-28) -> the phone (2-4.2) then the board (2-4.1) disconnected 5 s
  apart, both on the same hub, while the user left; not a board fault -> run stopped; re-run from the start.
- SAVED never seen by the phone for a store without check (2026-10-28) -> stored exactly, but `states` empty -> cause:
  the board notifies SAVED inside the commit write, before the write response, and BleOp's wait for the write
  response dropped every other event -> fix: BleOp keeps early STATUS notifications (the app handles them by callback
  and was not affected) -> where: BleOp.kt.
- Zero-length writes refused (2026-10-28) -> how found: PSK "" and SSID "" GATT 19 (0x13 Value Not Allowed); our
  handlers allow them -> cause: the SDK's ATT layer refuses every zero-length write (BFLB_HOST_PARAMETER_CHECK, att.c);
  the app wrote an empty passphrase for an open network, so setting up an open network could never have worked ->
  fix: the board clears SSID and passphrase at every connection (on_connected), the app skips the empty passphrase
  write; run.py tests "commit without SSID" in a fresh connection -> where: ble_pairing.c, MainActivity.kt, run.py.
- USB-C 1 power came and went on the laptop's ports twice (2026-10-28) -> the laptop's USB-C ports did not keep
  power (sleep or charging state) -> no change; the runner's first checks would show `USB power no`.
- The SDK printed the link key on every encryption (2026-10-28) -> `BLE LTK: 390e...` on the console -> fix:
  both prints removed by sdk_patch.py (conn.c, hci_core.c); the other key prints in the host are BT_DBG / PTS / BR-EDR
  only, not built -> where: sdk_patch.py, SECURITY.txt.
- The board paired while not on USB power (2026-10-28) -> how found: the same run, `pt_ble` said `USB power no`
  (the laptop port had stopped giving power) and pairing still completed -> cause: the host calls the consent
  callback (auth_pairing_confirm, our gate) only when the phone starts the pairing; after the board's own Security
  Request (sent on connect so the dialog appears) it skips it (smp.c) -> fix: the gate moved to on_connected: no
  USB power, connection refused before any security request; checked on battery: `ble: connection refused, not on
  USB power`, the phone got no connection -> where: firmware/bl616/ble_pairing.c, SECURITY.txt.
- LE Secure Connections pairing failed (2026-10-28) -> how found: board console `Unhandled SMP code 0x0c` then
  `ble: pairing failed, reason 5` -> cause: the SDK profile of our controller library (ble1m0s1bredr0,
  ble_common_readonly.cmake) forces CONFIG_DISABLE_BT_ECC, which compiles out the host's public-key and DH-key
  handlers, while the host still offers SC because the controller has the P-256 commands -> fix: sdk_patch.py turns
  ECC on for that profile (legacy pairing is no option: the passphrase would be crackable from a sniffed pairing);
  paired at once afterwards (`ble: paired, bonded=1`) -> where: firmware/bl616/sdk_patch.py, SECURITY.txt.
- Writes timed out right after pairing (2026-10-28) -> BleOp wrote while the phone was still bonding (bond state 11)
  -> fix: it waits for BONDED (10 s) after pressing -> where: BleOp.kt.
- Pairing dialog never pressed (2026-10-28) -> how found: `paired` FAIL "no dialog", every write GATT -1 -> cause:
  BleOp looked for English button texts, the phone runs a Russian UI ("Устан. сопряж.") -> fix: BleOp presses
  `android:id/button1` / `button2` by resource id, and taps the pairing notification when Android shows one instead
  of the dialog -> where: android/.../BleOp.kt pairDialog.
- USB-C 1 on the laptop gave no power (2026-10-28) -> `USB cable: not detected (PGOOD high)` after the user moved the
  charger cable to the laptop -> another laptop port -> `detected (PGOOD low)`.
- Second automated run: the BL616 hung and the watchdog reset it, the board came back as ttyACM1/ttyACM3 (2026-10-28)
  -> how found: `BL616 port missing` in the run log; `pt_status` said "reset by the watchdog"; by hand, `mem` alone
  printed nothing and the port vanished ~13 s later (the harness samples `mem` before every step; the first run's
  empty `bl_mem` was the same hang) -> cause: the SDK's board init makes the Wi-Fi RAM left over after the Wi-Fi
  buffers a TLSF heap ("WRAM"); with BLE's 32 KB exchange memory and the 26 NetHub buffers only 576 B are left, TLSF
  wrote its ~3.2 KB control block over the start of the BLE exchange memory and the heap's size wrapped, so `mem`'s
  block walk never ended -> fix: firmware/bl616/sdk_patch.py registers that heap only from 8 KB up (both images;
  nothing allocates from it by name); `mem` reads 176 608 B free before BLE, 157 844 B with BLE advertising.
  The renumbering: the phone holds ttyACM2 and the runner held the old ttyACM1 open across the reset, so the BL616
  came back as ttyACM1/ttyACM3 and every `/dev/ttyACM0` script lost it -> fix: tools/power-bringup/board.py
  `BL_PORT` / `K230_PORT` (/dev/serial/by-id, by USB interface), all runners use them.
- In the test firmware `pt_ble on` advertises on battery too (seen 2026-10-28, USB-C 1 out): only the pairing is
  gated on USB power there. The product starts BLE only in its USB mode (main.c usb_mode), so Step 1's "no
  advertising on battery" is checked on the product image (test106), not here.
- First automated run: no advertising seen (2026-10-28) -> how found: step 1 FAIL after 30 s, `pt_ble` said `off`
  -> cause: the `mem` hang below (the harness samples `mem` just before `pt_ble on`; the watchdog reset swallowed
  the command); first taken for a console still settling after the flash and patched with a retry in run.py, which
  was removed again once the hang was found -> fix: see the next-but-one bullet; run.py now checks "BLE started".
- USB-C 1 unplugged for laptop charging (2026-10-28) -> `pt_ble` showed `USB power no`: pairing would be refused ->
  a charger put on USB-C 1 (power only) before the run.
- A build container from 2026-10-25 spun at 100 % CPU for 43 h (2026-10-28) -> how found: the product build of
  firmware/bl616 took over 10 min; `docker ps` showed an older `make -j5` in the same directory -> cause: a make
  left running in the background on 2026-10-25 (exact reason not found) racing the new build in the same build
  tree -> fix: both containers stopped (docker stop by ID), the product image rebuilt from a clean start -> where:
  none (own mistake: check `docker ps` before a build).
- First flash after the EN press failed (2026-10-28) -> `make flash` ran the moment the port appeared and got
  "All programming failed"; a second `make flash` 3 s later passed -> no change (one retry by hand).
- The phone was on the VM's USB but not on adb (2026-10-28) -> USB debugging was off (the phone showed in MTP mode
  only) -> fix: Developer options -> USB debugging, the computer allowed on the phone (Samsung SM-G715FN, Android 13,
  Russian UI) -> where: Setup. The phone also adds a CDC ACM port (/dev/ttyACM2 here); the scripts use ttyACM0/1
  by name, so it does not collide, but a phone plugged in before the board would take ttyACM0.
- BLE start dropped the BL616's USB device (2026-10-28; the cause below is SUSPECTED, not proven) -> how found: `pt_ble on` printed the RF parameters, then
  the console port vanished (`usb 2-4.1: USB disconnect`); it never came back by itself, an EN press was needed;
  the phone still saw `LEAKCAM-B14A` advertising (RSSI -25), so the chip was alive -> cause: the SDK's BLE
  controller resets the PDS block in its init (weak hook `btblecontroller_software_pds_reset`,
  GLB_AHB_MCU_SW_PDS); the BL616's USB PHY is controlled from PDS, so USB died while everything else ran on and fed
  the watchdog, which therefore never fired. A/B so far: with the hook replaced by a print-only one the console
  stayed up through every BLE start since (4) - but those runs all had the corrupt WRAM heap below writing into the
  BLE exchange memory, and the sample is small; a clean A/B on the fixed heap (hook restored, 3 starts) decides
  -> fix (provisional): an empty `btblecontroller_software_pds_reset()` in
  firmware/bl616/ble_pairing.c (both images: no BLE sleep is used, nothing in PDS needs the reset) -> where:
  ble_pairing.c, DESIGN.md 5.3. Two EN presses by the user while finding it.
- Wi-Fi RAM overflow (2026-10-27) -> how found: the first build of the test firmware with BLE failed to link,
  `ram_wifi overflowed by 11712 bytes` -> cause: the BLE controller library (ble1m0s1bredr0) reserves 32 KB of exchange
  memory out of the BL616's 160 KB Wi-Fi RAM, and the 26 NetHub download buffers of 2026-10-25 (test100, 15 -> 26)
  filled the remaining 128 KB to 142.8 KB; the product image (BLE + Wi-Fi) had stopped linking with that change too,
  unnoticed (its last good .bin was built before it) -> fix: the BL616's own lwIP heap 18 -> 6 KB (it carries only DHCP,
  DNS and the checks' one HTTP GET; the K230's traffic bypasses it), no speed lost; Wi-Fi RAM at 99.6 % (130 496 of 131 072 B), both images
  -> where: firmware/bl616/lwipopts_user.h (LWIP_HEAP_SIZE, comment names this test). To verify on the board: the
  credential check and the BL616-only server check (test100 step 4) still pass with the smaller heap.
- 2026-11-03 05:58 UTC, the first run on the setup test bed: the VM's xHCI hung at the run's first `reboot` (dmesg
  hub_ext_port_status err -32, no BL616 port for 60 s: "BLE started (lp_boot)" failed). Stopped, xHCI rebound
  (0000:0a:00.0), run again with the rebind watcher (skill board-bringup / memory vmware-usb).
- 2026-11-03 06:03-06:06 UTC, the second run on the setup test bed (the phone 8 m away in another room, RSSI -70 to
  -77): 33/37; three sessions came back in 3-4 s with no STATUS (unknown SSID, the checked store, one store cycle),
  the runner logged nothing about why. The same sessions by hand right after: 6/6 (unknown SSID 0x10, the real network
  SAVED in 2.6-2.8 s). Fixed in run.py: sess() for every paired session: one that never left the phone is retried
  once and noted (as the store cycles did already), and a session without STATUS logs what the phone saw.
- 2026-11-03 06:09-06:14 UTC, the third run: 35/37; the commit without an SSID and the offset write came back with no
  write at all (GATT None: the session never connected, the phone 8 m away), every other session passed. Those
  sessions moved to sess() as well (a refusal is a status > 0, so the retry cannot hide one).
