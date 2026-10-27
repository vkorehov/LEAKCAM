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
- BLE central on the host: a USB Bluetooth dongle passed through to the Linux VM (BlueZ + `bleak`; the VM has no
  Bluetooth controller of its own, 2026-10-27), or `bleak` on the Mac. Script `tests/test040-ble-wifi-credentials/run.py`
  (harness) drives it with no hands.
- Networks: the test network from `~/.leakcam_wifi` (2.4 GHz, internet), a wrong passphrase for it, an SSID that
  does not exist; for the no-internet / captive cases a phone hotspot without data.
- USB-C 1 in (BLE only on USB power); the cell connected for the unplug steps.
- Probes: USB-C 1 VBUS meter (BLE advertising and connected current), VBAT at C15.1.

## Needs (build features and dependencies)

BL616: SDK btble (controller + host, SMP with LE Secure Connections, GATT server), `rfparam_init`, easyflash on
littlefs (PSM partition, `CONFIG_LITTLEFS_KV_EASYFLASH`), Wi-Fi fhost + lwIP + DHCP + DNS for the check
(`wifi_check.c`), USB power sense (`usb_present()`, test035). Host: BlueZ, Python `bleak`.

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
8. **Radio busy:** commit 0x01 while a K230 session holds Wi-Fi: BUSY, nothing stored.
9. **Repeats (leak test):** 20 connect / pair / write / commit 0x02 / disconnect cycles and 8 checked commits: BL616
   `mem` before and after each (mem_leak.py), easyflash still writes (its garbage collection runs), no reset.

## Pass

Advertising and pairing only on USB power; every write limit enforced; credentials stored exactly, only after a
passed check (0x01) or on request (0x02), never on a failed check or a dropped link; they survive resets, HBN and a
power cut; no leak over the repeats; the passphrase never appears on a console or in a log.

## Results

Not run yet.

## Metrics (**MISSING** on every run: skill leakcam-test-metrics)

Probes: current - USB-C 1 VBUS meter, 10 A range, plugged in with leads shorted first (test102 Setup); cell full, D1 off; voltage - VBAT C15.1: minimum under the test's load (on battery = the cell; on USB the charger rail).

| Metric | Value | Top / budget | Note |
|---|---|---|---|
| Boot, power-on -> READY (median of 5) | n/a: the K230 is not powered (BL616 only) | target (test022) | |
| This part's boot share | **MISSING**: BLE init (rfparam, controller, host) -> first advertisement on the BL616 | - | |
| Test run time | **MISSING** | - | setup (flashing, plugging, waiting) separately |
| provisioning times: connect + pair, write, store, check (JOINING -> SAVED) | **MISSING** | - | % of the top |
| Current idle / during (min / avg / peak) | **MISSING** | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |
| Voltage under load (min) | **MISSING** | cutoff 3.10 V (bug-004) | at the probes above |
| Power, energy per operation | **MISSING** | - | scripts/energy.py |
| Per day | n/a: on USB power only, once per installation | ENERGY.txt line | n per day |
| Temperature die / board (start / peak / end) | **MISSING** | K230 limit (datasheet) | chip_temp, AHT20 |
| Memory BL616 / K230 (start / min / end), leak | **MISSING** | no leak; budget (DESIGN.md) | BL616 `mem`, K230 `free`; repeated operations: mem_leak.py over >= 8 cycles (skill leakcam-test-metrics 5b) |
| Image (sizes) | **MISSING** | minimal image (rule) | the image this run used |
| Compile flags | **MISSING** | smallest code (rule) | |
| Features over the minimal image | **MISSING**: this test's Needs built on test008's floor | test008 floor | bytes, boot ms, RAM, current |
| Optimised build (-O3, LTO, -march) | **MISSING**: speed / size / boot / current vs the current flags | the faster one stays | |

## Boot time

BLE starts only on USB power, after the BL616's boot: its init to the first advertisement is measured here; the
battery wake path does not include it (to confirm: no BLE code runs on a battery wake).

## Battery energy

Provisioning runs on USB power only, so it costs the cell nothing; recorded: the VBUS current while advertising and
connected, and that BLE is fully off on battery (the sleep floor of test031 unchanged).

## Obstacles

None yet.
