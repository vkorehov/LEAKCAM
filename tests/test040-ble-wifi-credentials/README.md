# test040: BLE provisioning on the BL616: Wi-Fi credentials into flash

Status: **pass** (2026-10-29): run.py 46/46 (run 13, test firmware -O3 + LTO with the sdk_patch.py console fix) and
hand steps 1 and 2; step 5's full power cut not done (user: the cell stays in). Test firmware only.

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

## Result

- Steps 3-9 pass, 46 of 46 checks (run.py runs 12 and 13, 2026-10-29): every write limit enforced; credentials
  stored exactly (no old tail bytes); kept over HBN and a watchdog reset; checked store reports JOINING -> ADDRESS ->
  INTERNET -> SAVED, wrong passphrase 0x11 WRONG_PASSWORD, unknown SSID 0x10 NOT_FOUND with the old credentials kept;
  link dropped during a check: nothing stored; a second commit starts no second check; BLE and Wi-Fi run together.
- Step 8 (BLE first, during a session; run 13, -O3 + LTO, 2026-10-29): the session is back 0.1 s after a check during
  it, 0.1 s after SAVED (checked and unchecked), 3.4 s after a failed join to a missing network, 0.1 s after SAVED with
  a changed router password (run 12, -O2: 4.0 s after the failed join, 4.6 s after the changed password).
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
- Test firmware back to default build flags (-O2, no LTO; -O3 / LTO only for optimisation tests, user 2026-10-29);
  the -O3 / LTO comparison in Metrics stands (the product defconfig uses -O3 + LTO).

Open:
- Step 5: the full power cut (cell out, USB out) not done (user keeps the cell in); persistence over reset, watchdog
  reset and HBN passed.
- The default-flag (-O2) test firmware rebuild is not flashed yet (2026-10-29).
- The product firmware does not boot after a flash (no USB device, no advertising, 45-46 mA steady, at -O2 and -O3 +
  LTO; 2026-10-29) - left for the product rewrite.
- 2026-10-28: `pt_ble on` typed on battery advertised (only pairing gated in the test firmware) vs hand step 1 of
  2026-10-29: no advertising on battery after the unplug; an explicit `pt_ble on` on battery not rechecked since.
- Confirm no BLE code runs on a battery wake (Boot time).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the K230 is not powered (BL616 only) | target (test022) | |
| This part's boot share | `pt_ble on` -> advertising 0.290 s (median of 5 after watchdog resets, spread 1 ms): radio init (rfparam) 0.187 s, controller + host + advertising 0.103 s; host-timed over the USB console (2026-10-29) | - | |
| Test run time | 494 s automated (run.py, run 13 with step 8, 2026-10-29), no setup | - | setup (flashing, plugging, waiting) separately |
| provisioning times: connect + pair, write, store, check (JOINING -> SAVED) | advertising seen 2.1 s after BLE start; connect + pair 11.0 s; store 0x02 3.1 s per session; check 0x01 0x1 2.0 s -> 0x2 7.8 s -> 0x3 11.4 s -> 0x4 11.5 s (2026-10-28, run.py) | - | % of the top |
| Current idle / during (min / avg / peak) | USB-C 1 VBUS, cell full, test firmware -O3 + LTO (hand_steps.py meter, 2026-10-29): BLE off idle 60 / 60.3 mA; advertising 63.4 / peak 63.5 mA; phone connected, idle link 63.1 / 63.2 mA; Wi-Fi check (14 s, JOINING -> SAVED) peak 86 mA. BLE costs ~3.4 mA over idle. With the cell charging the meter read 443 -> 370 -> 144 mA (charge current, not the board) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) (2026-10-28) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | at ~5 V VBUS: idle 0.30 W, advertising 0.32 W, check peak 0.43 W; one check <= 86 mA x 5 V x 14 s = 6 J from USB (upper bound, peak held the whole check), ~1.6 J over the connected level; the battery pays nothing: BLE runs on USB power only (2026-10-29) | - | scripts/energy.py |
| Per day | n/a: on USB power only, once per installation | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 32.9 / 32.9 / 32.9 C (AHT20) (2026-10-28) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | run 13 (-O3 + LTO): BL616 free 105340 -> 105336 B, min 94568, 170 blocks over 20 cycles: no leak; run 12 (-O2, the test firmware's flags now): free 106052 / min 97420 B, 169 blocks, unchanged over 20 cycles with BLE first and session restarts (2026-10-29, run.py) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 test firmware 1633088 B (-O3 + LTO, run 13, 2026-10-29); 1305360 B at the default -O2 (2026-10-28, on the board at that run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (back to default flags, user 2026-10-29: -O3 / LTO only for optimisation tests; run 13 was -O3 + LTO); BL616 product defconfig -O3 + LTO (2026-10-29) | smallest code (rule) | |
| Features over the minimal image | BLE in the product image (leakcam_bl616.map, -O2): controller 113.8 KB + host 66.1 KB + ble_pairing.c 2.1 KB = 182 KB flash (14.8 % of 1233 KB), 14.2 KB static RAM; at run time 18.8 KB heap and 5 tasks (`mem` before / after `pt_ble on`: free 176528 -> 157764 B); start 0.29 s, only on USB power, not on the boot path (2026-10-29) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | measure_build.py on the test firmware, 2026-10-29: -O2 / -O3 / -O3 + LTO: image 1330080 / 1727920 / 1637200 B; BLE start -> advertising 0.292 / 0.277 / 0.275 s; remembered-AP join (associate + WPA, median of 5) 272 / 250 / 168 ms; full join 3195 / 3170 / 3090 ms; heap after BLE start 157764 / 152260 / 157060 B free. -O3 + LTO adopted in the product defconfig (the test firmware is back to -O2, user 2026-10-29); -march is already the E907's full set (SDK: -march=rv32imafcpzpsfoperand_xtheade -mtune=e907, nothing wider); current not compared (meter, hand step) | the faster one stays | |

## Boot time

BLE starts only on USB power, after the BL616's boot: its init to the first advertisement is measured here; the
battery wake path does not include it (to confirm: no BLE code runs on a battery wake).

## Battery energy

Provisioning runs on USB power only, so it costs the cell nothing; recorded: the VBUS current while advertising and
connected, and that BLE is fully off on battery (the sleep floor of test031 unchanged).
