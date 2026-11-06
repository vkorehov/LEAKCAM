# test040: BLE provisioning on the BL616: Wi-Fi credentials into flash

Status: **pass** (2026-11-03): run.py 37/37 in 368 s on the setup firmware's test bed (firmware/bl616_setup's own
sources + pt_ble), the phone 8 m away in another room: the check ends at the API (JOINING -> ADDRESS -> INTERNET ->
SAVED), every write limit, the PIN gate, persistence over a reset and a watchdog reset, 8 store cycles without a leak.
Step 8 (a check during a K230 session) is gone with the firmware split (the setup firmware runs no session); hand
steps 1 and 2 last done 2026-10-29 on the test firmware; step 5's full power cut not done (user: the cell stays in).

The BL616's one BLE job (firmware/bl616_setup/ble_pairing.c): it advertises as its serial (the device ID, no prefix), accepts pairing with the
board's pre-stored PIN (LE Secure Connections passkey entry, since 2026-11-01; Just Works refused), takes the SSID and passphrase over its provisioning service and stores them in easyflash
(`wifi_ssid`, `wifi_psk`), after its own check (commit 0x01: join, DHCP, generate_204) or without one (0x02). This
test checks the BL616 side from a BLE central on the host, characteristic by characteristic; the phone app and the
product flow are test106.

## Setup

- The setup firmware's test bed (tests/firmware/bl616_setup_test, `run.py --flash` or make flash there; since
  2026-11-03, before: the power test firmware): firmware/bl616_setup's own sources started by `lp_boot`, its switch
  to the full firmware off (`switch_to_full off`: every store stays here), and the power test firmware's `pt_ble`:
  `pt_ble creds` (lengths and a CRC of the stored `wifi_ssid` / `wifi_psk`, never the passphrase itself),
  `pt_ble erase` (delete both keys and `wifi_ap`). Console PR1 (`/dev/leakcam-bl616`).
- BLE central: the user's Android phone on adb over USB (2026-10-27; the VM has no Bluetooth controller of its own).
  `android/app/src/androidTest/.../BleOp.kt` does one BLE operation per call (scan, unpair, session: pair accept /
  cancel, MTU, hex writes, the STATUS notifications, a link drop on a given state) and accepts the system pairing
  dialog with UI Automator; `run.py` (harness) chains the calls and checks the board between them, no hands. APKs:
  `./gradlew assembleDebug assembleDebugAndroidTest` (android/README.md); the runner installs both with the
  permissions granted, keeps the screen on while plugged. The phone: unlocked, no other use during the run.
- Networks: the test network from `~/.leakcam_wifi` (2.4 GHz, internet), a wrong passphrase for it, an SSID that
  does not exist; for the no-internet / captive cases a phone hotspot without data.
- USB-C 1 in; the board's pairing PIN pre-stored (make flash), the presence server reachable (the PIN for the
  sessions comes from it, as for the app).
- Probes: USB-C 1 VBUS meter (BLE advertising and connected current), VBAT at C15.1.

## Needs (build features and dependencies)

Phone: the LEAKCAM Setup APK and its test APK (androidx.test runner, ext.junit, UI Automator: test-only
dependencies). BL616: SDK btble (controller + host, SMP with LE Secure Connections, GATT server), `rfparam_init`, easyflash on
littlefs (PSM partition, `CONFIG_LITTLEFS_KV_EASYFLASH`), Wi-Fi fhost + lwIP + DHCP + DNS for the check
(`wifi_check.c`), USB power sense (`usb_present()`, test035).

(What this test uses; the union over all tests is the feature set the minimal image keeps.)

## Steps

1. **Advertising:** the serial (= the USB serial number) advertised with the service UUID, time from boot to the first advertisement. (The
   USB power gate is gone since 2026-11-01; the battery windows of the product are test106 step 7.)
2. **Authentication gate:** connect without pairing, write SSID: refused; pair with the PIN (as the app): accepted.
   A stranger's phone is dropped (user, 2026-11-04: no neighbour holds the board): one that never pairs 10 s after it
   connected, one whose pairing fails (no PIN) at once; the owner's app connects right after.
3. **Characteristic limits** (MTU 247 first, as the app): SSID 1 and 32 bytes accepted, 33 refused; passphrase 8 and
   63 accepted, 7 and 64 refused, empty accepted (open network); a write at offset > 0 refused; commit without SSID,
   with a value other than 0x01 / 0x02 or longer than 1 byte refused.
4. **Store unchecked (0x02):** STATUS notifies SAVED; `pt_ble creds` matches what was written (length + CRC), no
   passphrase on the console or in the log.
5. **Persistence:** the stored credentials survive a BL616 reset, a watchdog reset (`crash hang`), and a full
   power cut (cell out, USB out); `pt_wifi join` with the stored keys joins (test100 path).
6. **Overwrite:** a shorter SSID and passphrase after longer ones: read back exactly (no old tail bytes); the
   remembered AP (`wifi_ap`) of the old network does not stop the join on the new one.
7. **Checked store (0x01):** right credentials: JOINING -> ADDRESS -> INTERNET -> SAVED, with the time of each; wrong
   passphrase: WRONG_PASSWORD, the old credentials kept; unknown SSID: NOT_FOUND; hotspot without data: NO_INTERNET
   (or NO_DNS); the central disconnects during the check: nothing stored; a second commit during the check: no second
   check.
8. **BLE first, new network during a K230 session**: gone since 2026-11-02 (all BLE in the setup firmware, which runs
   no K230 session; the full firmware has no BLE): not in run.py since 2026-11-03. Its old result: HISTORY.md.
9. **Repeats (leak test):** 8 connect / pair / write / commit 0x02 / disconnect cycles (the leak verdict's minimum; a cycle
   that never left the phone is retried once and noted) and 8 checked commits: BL616
   `mem` before and after each (mem_leak.py), easyflash still writes (its garbage collection runs), no reset.

## Pass

Advertising and pairing as the design says (USB power only when this test ran; on battery too since 2026-11-01,
test116); every write limit enforced; credentials stored exactly, only after a
passed check (0x01) or on request (0x02), never on a failed check or a dropped link; a check during a K230
session runs and gives the radio back; new credentials restart the session on them; they survive resets, HBN and a
power cut; no leak over the repeats; the passphrase never appears on a console or in a log.

## Result

- 2026-11-03 06:15-06:21 UTC, run.py 37/37, 368 s, setup_test 0.6+g8828b50-dirty-202611030525 (the test bed,
  `switch_to_full off`), USB-C 1 in, the phone 8 m away in another room: advertising seen 4.7 s after `lp_boot`
  (RSSI -62); the cancelled pairing and the pairing without the PIN: every write refused, nothing stored; paired with
  the PIN (passkey entry), MTU 247; every write limit enforced; stored exactly (no old tail bytes); kept over a reset
  and a watchdog reset (`crash hang`, the record cleared after); wrong passphrase 0x11, unknown SSID 0x10, the old
  credentials kept; the link dropped during a check: the board finishes and stores; checked store JOINING ->
  ADDRESS -> INTERNET -> SAVED, a second commit starts no second check; 8 store cycles, BL616 free flat (no leak).
- Checked store by hand right after the second run (2026-11-03, same sessions, 3 each): JOINING -> SAVED 2.6-2.8 s,
  unknown SSID 0x10 in 4.6 s.
- Three runs before it lost sessions that never left the phone at 8 m (HISTORY.md Obstacles): run.py retries such a
  session once and notes it (sess()); the passing run needed no retry.
- Kept from before (HISTORY.md): no passphrase or key dump on the console (measure_build.py, 2026-10-29); the empty
  PDS reset hook keeps USB (pds_ab.py, 2026-10-29); no BLE bonds kept (the phone pairs again after a reset).

Open:
- Step 5: the full power cut (cell out, USB out) not done (user keeps the cell in); persistence over a reset and a
  watchdog reset passed.
- Hand steps 1 and 2 on the setup firmware (battery: PDS sleep with 2 s advertising, DESIGN.md 5.3.1, test120): last
  run on the test firmware (2026-10-29).

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the K230 is not powered (BL616 only) | target (test022) | |
| This part's boot share | `pt_ble on` -> advertising 0.290 s (median of 5 after watchdog resets, spread 1 ms): radio init (rfparam) 0.187 s, controller + host + advertising 0.103 s; host-timed over the USB console (2026-10-29) | - | |
| Test run time | 368 s automated (run.py, 2026-11-03, the setup test bed), no setup | - | setup (flashing, plugging, waiting) separately |
| provisioning times: connect + pair, write, store, check (JOINING -> SAVED) | advertising seen 4.7 s after lp_boot (RSSI -62, 8 m); checked store JOINING -> ADDRESS -> INTERNET -> SAVED 2.6-2.8 s (by hand, same sessions); unknown SSID 0x10 4.6 s (2026-11-03) | - | % of the top |
| Current idle / during (min / avg / peak) | USB-C 1 VBUS, cell full, test firmware -O3 + LTO (hand_steps.py meter, 2026-10-29): BLE off idle 60 / 60.3 mA; advertising 63.4 / peak 63.5 mA; phone connected, idle link 63.1 / 63.2 mA; Wi-Fi check (14 s, JOINING -> SAVED) peak 86 mA. BLE costs ~3.4 mA over idle. With the cell charging the meter read 443 -> 370 -> 144 mA (charge current, not the board) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | n/a: BL616-only test, rails off - VBAT is read by the K230 ADC (test023) (2026-10-28) | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | at ~5 V VBUS: idle 0.30 W, advertising 0.32 W, check peak 0.43 W; one check <= 86 mA x 5 V x 14 s = 6 J from USB (upper bound, peak held the whole check), ~1.6 J over the connected level; the battery pays nothing: BLE runs on USB power only (2026-10-29) | - | scripts/energy.py |
| Per day | n/a: on USB power only, once per installation | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | die n/a: K230 off; board 32.9 / 32.9 / 32.9 C (AHT20) (2026-10-28) | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | setup test bed: BL616 free 161296 / min 135076 / 161008 B, blocks 96 -> 101; 8 store cycles: no leak (free 161008 -> 161008 B, mem_leak.py) (2026-11-03) | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | BL616 setup test bed 1288672 B (-Os -g, 2026-11-03, on the board at the run) | minimal image (rule) | the image this run used |
| Compile flags | K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + --gc-sections, strip, no LTO; BL616 test firmware -O2 (SDK default), no LTO (back to default flags, user 2026-10-29: -O3 / LTO only for optimisation tests; run 13 was -O3 + LTO); BL616 product defconfig -O3 + LTO (2026-10-29) | smallest code (rule) | |
| Features over the minimal image | BLE in the product image (leakcam_bl616.map, -O2): controller 113.8 KB + host 66.1 KB + ble_pairing.c 2.1 KB = 182 KB flash (14.8 % of 1233 KB), 14.2 KB static RAM; at run time 18.8 KB heap and 5 tasks (`mem` before / after `pt_ble on`: free 176528 -> 157764 B); start 0.29 s, only on USB power, not on the boot path (2026-10-29) | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | measure_build.py on the test firmware, 2026-10-29: -O2 / -O3 / -O3 + LTO: image 1330080 / 1727920 / 1637200 B; BLE start -> advertising 0.292 / 0.277 / 0.275 s; remembered-AP join (associate + WPA, median of 5) 272 / 250 / 168 ms; full join 3195 / 3170 / 3090 ms; heap after BLE start 157764 / 152260 / 157060 B free. -O3 + LTO adopted in the product defconfig (the test firmware is back to -O2, user 2026-10-29); -march is already the E907's full set (SDK: -march=rv32imafcpzpsfoperand_xtheade -mtune=e907, nothing wider); current not compared (meter, hand step) | the faster one stays | |

## Boot time

BLE started only on USB power when this ran (since 2026-11-01 from boot, also on battery), after the BL616's boot: its init to the first advertisement is measured here; the
battery wake path does not include it (to confirm: no BLE code runs on a battery wake).

## Battery energy

Provisioning ran on USB power only then, so it cost the cell nothing (on battery since 2026-11-01: test116, test120); recorded: the VBUS current while advertising and
connected, and that BLE is fully off on battery (the sleep floor of test031 unchanged).
