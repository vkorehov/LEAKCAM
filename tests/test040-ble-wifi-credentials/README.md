# test040: BLE provisioning on the BL616: Wi-Fi credentials into flash

Status: **planned** (2026-10-27).

The BL616's one BLE job (firmware/bl616/ble_pairing.c): on USB power it advertises as `LEAKCAM-xxyy`, accepts Just
Works pairing, takes the SSID and passphrase over its provisioning service and stores them in easyflash
(`wifi_ssid`, `wifi_psk`), after its own check (commit 0x01: join, DHCP, generate_204) or without one (0x02). This
test checks the BL616 side from a BLE central on the host, characteristic by characteristic; the phone app and the
product flow are test106.

## Setup

- BL616 test firmware (tools/power-bringup/bl616_powertest) with BLE added: `ble_pairing.c` + `wifi_check.c` built
  in (today's defconfig has no BLE), and a `pt_ble` shell command: `pt_ble` (advertising, connection, last status),
  `pt_ble creds` (lengths and a CRC of the stored `wifi_ssid` / `wifi_psk`, never the passphrase itself),
  `pt_ble erase` (delete both keys and `wifi_ap`). Console PR1 (`/dev/ttyACM0`).
- BLE central: the user's Android phone on adb over USB (2026-10-27; the VM has no Bluetooth controller of its own).
  `android/app/src/androidTest/.../BleOp.kt` does one BLE operation per call (scan, unpair, session: pair accept /
  cancel, MTU, hex writes, the STATUS notifications, a link drop on a given state) and accepts the system pairing
  dialog with UI Automator; `run.py` (harness) chains the calls and checks the board between them, no hands. APKs:
  `./gradlew assembleDebug assembleDebugAndroidTest` (android/README.md); the runner installs both with the
  permissions granted, keeps the screen on while plugged. The phone: unlocked, no other use during the run.
- Networks: the test network from `~/.leakcam_wifi` (2.4 GHz, internet), a wrong passphrase for it, an SSID that
  does not exist; for the no-internet / captive cases a phone hotspot without data.
- USB-C 1 in (BLE only on USB power); the cell connected for the unplug steps.
- Probes: USB-C 1 VBUS meter (BLE advertising and connected current), VBAT at C15.1.

## Needs (build features and dependencies)

Phone: the LEAKCAM Setup APK and its test APK (androidx.test runner, ext.junit, UI Automator: test-only
dependencies). BL616: SDK btble (controller + host, SMP with LE Secure Connections, GATT server), `rfparam_init`, easyflash on
littlefs (PSM partition, `CONFIG_LITTLEFS_KV_EASYFLASH`), Wi-Fi fhost + lwIP + DHCP + DNS for the check
(`wifi_check.c`), USB power sense (`usb_present()`, test035).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **Advertising gate:** USB in: `LEAKCAM-xxyy` advertised with the service UUID, time from the plug (or boot) to the
   first advertisement; USB out: no advertising, any connection dropped (`ble_pairing_stop`); plug back: again.
2. **Encryption gate:** connect without pairing, write SSID: refused (insufficient encryption); pair (Just Works):
   accepted on USB power; pairing attempt with USB out (connect just before the unplug): refused, nothing written.
3. **Characteristic limits** (MTU 247 first, as the app): SSID 1 and 32 bytes accepted, 33 refused; passphrase 8 and
   63 accepted, 7 and 64 refused, empty accepted (open network); a write at offset > 0 refused; commit without SSID,
   with a value other than 0x01 / 0x02 or longer than 1 byte refused.
4. **Store unchecked (0x02):** STATUS notifies SAVED; `pt_ble creds` matches what was written (length + CRC), no
   passphrase on the console or in the log.
5. **Persistence:** the stored credentials survive a BL616 reset, a watchdog reset, HBN sleep and wake, and a full
   power cut (cell out, USB out); `pt_wifi join` with the stored keys joins (test100 path).
6. **Overwrite:** a shorter SSID and passphrase after longer ones: read back exactly (no old tail bytes); the
   remembered AP (`wifi_ap`) of the old network does not stop the join on the new one.
7. **Checked store (0x01):** right credentials: JOINING -> ADDRESS -> INTERNET -> SAVED, with the time of each; wrong
   passphrase: WRONG_PASSWORD, the old credentials kept; unknown SSID: NOT_FOUND; hotspot without data: NO_INTERNET
   (or NO_DNS); the central disconnects during the check: nothing stored; a second commit during the check: no second
   check.
8. **BLE first, new network during a K230 session** (user, 2026-10-28; the bridge `pt_wifi bridge` stands in for
   the session, `pt_wifi link` reads its association): commit 0x01 with a wrong passphrase while the bridge is on
   the stored network: the check runs (no BUSY), WRONG_PASSWORD, old credentials kept, the bridge back on the
   network; commit 0x01 with the right ones: SAVED, the session restarts and is back on the network (time from
   SAVED); commit 0x02 of another network: the session restarts on it (gone from the old one); the real network
   stored again after that failed join: back on it (time); the router's password changed (0x02 of a wrong one: the
   session retries and fails), then the new one with a check during those retries: SAVED, back on it (time).
9. **Repeats (leak test):** 20 connect / pair / write / commit 0x02 / disconnect cycles and 8 checked commits: BL616
   `mem` before and after each (mem_leak.py), easyflash still writes (its garbage collection runs), no reset.

## Pass

Advertising and pairing only on USB power; every write limit enforced; credentials stored exactly, only after a
passed check (0x01) or on request (0x02), never on a failed check or a dropped link; a check during a K230
session runs and gives the radio back; new credentials restart the session on them; they survive resets, HBN and a
power cut; no leak over the repeats; the passphrase never appears on a console or in a log.

## Results

2026-10-28 19:06 (run.py, run 5, test firmware with ble_pairing.c and every fix above, phone SM-G715FN): **pass**,
36 of 36 checks. Pairing hands-free (both Samsung dialogs answered by BleOp), MTU 247; every write limit enforced;
stored exactly (32/63, then 6/8 bytes); kept over HBN and a watchdog reset; checked store: wrong passphrase
JOINING -> 0x11 WRONG_PASSWORD, unknown SSID -> 0x10 NOT_FOUND, old kept each time; link dropped during a check:
nothing stored; the real network JOINING -> ADDRESS -> INTERNET -> SAVED with the phone connected over BLE all the
time (BLE and Wi-Fi together); a second commit started no second check; BUSY while the bridge had the radio (since
replaced: BLE first, step 8); 20 store cycles, no leak (BL616 free 106104 -> 106100 B). Earlier partial run
(08:44, phone and board left the VM's USB at 08:49) superseded by it. Runs 6-8 (19:29-19:41) void: run 6 was
never stopped and drove the board and phone under runs 7 and 8 (Obstacles).

2026-10-28 22:05 (run.py, run 11, test firmware with wifi_link_sta_leave()): 44 of 44 checks passed up to repeat
cycle 7, then the board left USB (the user stopped for the day). Step 8 in full: a check during a session runs and
the session rejoins; checked and unchecked stores restart the session on the new network; the real network back
4.7 s after a failed join to a missing network; router password changed (session in auth retries), the new one
checked during the retries: JOINING -> ADDRESS -> INTERNET -> SAVED, the session back 4.7 s after SAVED. To finish:
the 20 repeats (leak verdict), the hand steps 1, 2, 5, the current metrics, the feature-cut and optimised builds.

### Automated run 2026-10-28 (run.py, 86 s): **FAIL**

the board was never seen advertising.

### Automated run 2026-10-28 (run.py, 129 s): **FAIL**

the board was never seen advertising.

### Automated run 2026-10-28 (run.py, 417 s): pass

36 of 36 checks passed. Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- bond after HBN sleep and wake: lost, paired again (CCC write GATT -1)
- bond after a watchdog reset: lost, paired again (CCC write GATT -1)

### Automated run 2026-10-28 (run.py, 36 s): **FAIL**

the board was never seen advertising.

- phone USB port was the power source: swapped to sink

### Automated run 2026-10-28 (run.py, 521 s): **FAIL**

42 of 43 checks passed; failed: the real network stored again: the session restarts on it (-1.0 s) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink

### Automated run 2026-10-28 (run.py, 447 s): **FAIL**

42 of 43 checks passed; failed: the real network stored again after a failed join: the session restarts on it (-1.0 s) Not automated: advertising stops on unplug, pairing refused on battery, power cut (README Steps 1, 2, 5).

- phone USB port was the power source: swapped to sink

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the K230 is not powered (BL616 only) | target (test022) | |
| This part's boot share | **MISSING**: BLE init (rfparam, controller, host) -> first advertisement on the BL616 | - | |
| Test run time | 417 s automated (run.py, run 5, 2026-10-28), no setup | - | setup (flashing, plugging, waiting) separately |
| provisioning times: connect + pair, write, store, check (JOINING -> SAVED) | advertising seen 2.1 s after BLE start; connect + pair 11.0 s; store 0x02 3.1 s per session; check 0x01 0x1 2.0 s -> 0x2 7.8 s -> 0x3 11.4 s -> 0x4 11.5 s (2026-10-28, run.py) | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) (2026-10-28) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | n/a: on USB power only, once per installation | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 32.9 / 32.9 / 32.9 C (AHT20) (2026-10-28) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | BL616 free 175072 / min 97996 / 106100 B, blocks 19 -> 169, deleted 0; BL616 no leak (mem_leak.py, 21 snapshots) (2026-10-28) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1305360 B (2026-10-28, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (2026-10-28) | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

BLE starts only on USB power, after the BL616's boot: its init to the first advertisement is measured here; the
battery wake path does not include it (to confirm: no BLE code runs on a battery wake).

## Battery energy

Provisioning runs on USB power only, so it costs the cell nothing; recorded: the VBUS current while advertising and
connected, and that BLE is fully off on battery (the sleep floor of test031 unchanged).

## Obstacles

- Wi-Fi RAM overflow (2026-10-27) -> how found: the first build of the test firmware with BLE failed to link,
  `ram_wifi overflowed by 11712 bytes` -> cause: the BLE controller library (ble1m0s1bredr0) reserves 32 KB of exchange
  memory out of the BL616's 160 KB Wi-Fi RAM, and the 26 NetHub download buffers of 2026-10-25 (test100, 15 -> 26)
  filled the remaining 128 KB to 142.8 KB; the product image (BLE + Wi-Fi) had stopped linking with that change too,
  unnoticed (its last good .bin was built before it) -> fix: the BL616's own lwIP heap 18 -> 6 KB (it carries only DHCP,
  DNS and the checks' one HTTP GET; the K230's traffic bypasses it), no speed lost; Wi-Fi RAM at 99.6 % (130 496 of 131 072 B), both images
  -> where: firmware/bl616/lwipopts_user.h (LWIP_HEAP_SIZE, comment names this test). To verify on the board: the
  credential check and the BL616-only server check (test100 step 4) still pass with the smaller heap.

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
- The phone was on the VM's USB but not on adb (2026-10-28) -> USB debugging was off (the phone showed in MTP mode
  only) -> fix: Developer options -> USB debugging, the computer allowed on the phone (Samsung SM-G715FN, Android 13,
  Russian UI) -> where: Setup. The phone also adds a CDC ACM port (/dev/ttyACM2 here); the scripts use ttyACM0/1
  by name, so it does not collide, but a phone plugged in before the board would take ttyACM0.
- First flash after the EN press failed (2026-10-28) -> `make flash` ran the moment the port appeared and got
  "All programming failed"; a second `make flash` 3 s later passed -> no change (one retry by hand).
- A build container from 2026-10-25 spun at 100 % CPU for 43 h (2026-10-28) -> how found: the product build of
  firmware/bl616 took over 10 min; `docker ps` showed an older `make -j5` in the same directory -> cause: a make
  left running in the background on 2026-10-25 (exact reason not found) racing the new build in the same build
  tree -> fix: both containers stopped (docker stop by ID), the product image rebuilt from a clean start -> where:
  none (own mistake: check `docker ps` before a build).
- USB-C 1 unplugged for laptop charging (2026-10-28) -> `pt_ble` showed `USB power no`: pairing would be refused ->
  a charger put on USB-C 1 (power only) before the run.
- First automated run: no advertising seen (2026-10-28) -> how found: step 1 FAIL after 30 s, `pt_ble` said `off`
  -> cause: the `mem` hang below (the harness samples `mem` just before `pt_ble on`; the watchdog reset swallowed
  the command); first taken for a console still settling after the flash and patched with a retry in run.py, which
  was removed again once the hang was found -> fix: see the next-but-one bullet; run.py now checks "BLE started".
- In the test firmware `pt_ble on` advertises on battery too (seen 2026-10-28, USB-C 1 out): only the pairing is
  gated on USB power there. The product starts BLE only in its USB mode (main.c usb_mode), so Step 1's "no
  advertising on battery" is checked on the product image (test106), not here.

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
- USB-C 1 on the laptop gave no power (2026-10-28) -> `USB cable: not detected (PGOOD high)` after the user moved the
  charger cable to the laptop -> another laptop port -> `detected (PGOOD low)`.
- Pairing dialog never pressed (2026-10-28) -> how found: `paired` FAIL "no dialog", every write GATT -1 -> cause:
  BleOp looked for English button texts, the phone runs a Russian UI ("Устан. сопряж.") -> fix: BleOp presses
  `android:id/button1` / `button2` by resource id, and taps the pairing notification when Android shows one instead
  of the dialog -> where: android/.../BleOp.kt pairDialog.
- Writes timed out right after pairing (2026-10-28) -> BleOp wrote while the phone was still bonding (bond state 11)
  -> fix: it waits for BONDED (10 s) after pressing -> where: BleOp.kt.
- LE Secure Connections pairing failed (2026-10-28) -> how found: board console `Unhandled SMP code 0x0c` then
  `ble: pairing failed, reason 5` -> cause: the SDK profile of our controller library (ble1m0s1bredr0,
  ble_common_readonly.cmake) forces CONFIG_DISABLE_BT_ECC, which compiles out the host's public-key and DH-key
  handlers, while the host still offers SC because the controller has the P-256 commands -> fix: sdk_patch.py turns
  ECC on for that profile (legacy pairing is no option: the passphrase would be crackable from a sniffed pairing);
  paired at once afterwards (`ble: paired, bonded=1`) -> where: firmware/bl616/sdk_patch.py, SECURITY.txt.
- The board paired while not on USB power (2026-10-28) -> how found: the same run, `pt_ble` said `USB power no`
  (the laptop port had stopped giving power) and pairing still completed -> cause: the host calls the consent
  callback (auth_pairing_confirm, our gate) only when the phone starts the pairing; after the board's own Security
  Request (sent on connect so the dialog appears) it skips it (smp.c) -> fix: the gate moved to on_connected: no
  USB power, connection refused before any security request; checked on battery: `ble: connection refused, not on
  USB power`, the phone got no connection -> where: firmware/bl616/ble_pairing.c, SECURITY.txt.
- The SDK printed the link key on every encryption (2026-10-28) -> `BLE LTK: 390e...` on the console -> fix:
  both prints removed by sdk_patch.py (conn.c, hci_core.c); the other key prints in the host are BT_DBG / PTS / BR-EDR
  only, not built -> where: sdk_patch.py, SECURITY.txt.
- USB-C 1 power came and went on the laptop's ports twice (2026-10-28) -> the laptop's USB-C ports did not keep
  power (sleep or charging state) -> no change; the runner's first checks would show `USB power no`.
- Zero-length writes refused (2026-10-28) -> how found: PSK "" and SSID "" GATT 19 (0x13 Value Not Allowed); our
  handlers allow them -> cause: the SDK's ATT layer refuses every zero-length write (BFLB_HOST_PARAMETER_CHECK, att.c);
  the app wrote an empty passphrase for an open network, so setting up an open network could never have worked ->
  fix: the board clears SSID and passphrase at every connection (on_connected), the app skips the empty passphrase
  write; run.py tests "commit without SSID" in a fresh connection -> where: ble_pairing.c, MainActivity.kt, run.py.
- SAVED never seen by the phone for a store without check (2026-10-28) -> stored exactly, but `states` empty -> cause:
  the board notifies SAVED inside the commit write, before the write response, and BleOp's wait for the write
  response dropped every other event -> fix: BleOp keeps early STATUS notifications (the app handles them by callback
  and was not affected) -> where: BleOp.kt.
- Phone and board left the VM at 08:49 (2026-10-28) -> the phone (2-4.2) then the board (2-4.1) disconnected 5 s
  apart, both on the same hub, while the user left; not a board fault -> run stopped; re-run from the start.
- "Writes refused without pairing" passed on -1 (never delivered) as if refused (2026-10-28, own mistake) -> run.py
  counts only a real GATT error (> 0) as a refusal; after a cancelled pairing the link drops, so that check is now
  "no write succeeds" (!= 0) plus "nothing stored".
- The phone dropped off USB during the APK install (2026-10-28 19:05) -> the test APK install failed, the phone came
  back as a new USB device seconds later -> install repeated (`adb install -r -g -t`), passed.
- Own mistake: a process listing printed the runner's `adb shell am instrument` command line, which carries the SSID
  and passphrase as hex (`-e writes ssid:...,psk:...`) (2026-10-28) -> only in the local terminal, nothing committed
  or sent; the passphrase was the wrong one of step 7, one character from the real one -> rule: never list the
  runner's child processes with their arguments; the values are visible to any local user through /proc while a
  call runs (bench only: the product app never puts them on a command line).
- Run 6: pairing "no dialog", every write -1 (2026-10-28 19:30) -> how found: `dumpsys window` showed
  `isKeyguardShowing=true`; the pairing dialog opened over the lock screen and closed after 30 s, UI Automator
  cannot press through the lock screen -> fix: run.py stops at the start when the phone is locked; the bench phone
  needs its screen lock off (or Stay awake) -> the user unlocks it.
- The phone powered the Mac instead of charging (2026-10-28 19:35) -> the user saw it; `dumpsys usb`: port0
  `power_role=source` (USB-C role negotiation picked the phone as source), battery down to 3 % -> fix:
  `dumpsys usb set-port-roles port0 sink device` (phone charging at +450 mA after it); run.py swaps the role at the
  start when it finds the phone as source.

None in the run of 2026-10-28.
- Own mistake: runs 6, 7 and 8 overlapped (2026-10-28 19:29-19:41) -> how found: run 8 found `t040-rep7` stored (a
  repeat-cycle value run 8 never wrote) and the bridge's rails on; run 6's log went on to its repeats -> cause: run 6
  was started as `make flash && ... && nohup run.py &`, so `$!` was the shell of that chain, not the runner; the
  "stop" killed only that shell and run 6 ran on, the phone pairing popups of two runners crossed (the "hanging
  popup" the user saw), and run 7's failed scan, run 8's failed sessions and its "link dropped: stored" all came
  from it -> fix: run 6's runner stopped by its PID, the board reset by the watchdog, run.py takes a lock (a second
  runner exits at once); runs started with the runner itself as the background job -> runs 6-8 discarded.
- Popup unanswered for 15 s after each board reset (2026-10-28) -> run.py probed the old bond with a session that
  presses nothing; the board keeps no bonds (CONFIG_BT_SETTINGS off), so it asked to pair and the dialog waited out
  its timeout -> fix: run.py forgets the board on the phone and pairs again after a reset, no probe.

None in the run of 2026-10-28.

None in the run of 2026-10-28.
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
- Every BL616 build took over 10 min (2026-10-28) -> the SDK's project.build deletes CMakeCache.txt and CMakeFiles
  and regenerates autoconfig.h on each make, so the whole SDK recompiled -> fix: firmware/bl616/host.mk builds in
  build/ directly while defconfig, Kconfig and CMakeLists.txt are older than the cache (5 s unchanged, ~50 s for one
  file), the SDK's full path otherwise.
- The phone powered the laptop again after a reconnect (2026-10-28 20:3x, user) -> the role swap holds only until
  the next USB reconnect -> tools/android-bench/phone_sink.sh re-applies it on every attach (run in the background;
  a permanent user service is the user's to install, README.txt there).
