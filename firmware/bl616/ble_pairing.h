/*
 * BLE, used for one thing only: setting the Wi-Fi credentials (and checking them
 * before they are stored).
 *
 * The device advertises as its serial (device_id.h) and exposes one provisioning service. Its characteristics
 * need an authenticated link: LE Secure Connections passkey entry with a per-device PIN, stored on
 * the board at manufacture (easyflash "pair_pin", `prov pin`); only the server knows how it is
 * derived and gives it to the app (GET /v1/devices/<id>/pin) right before it pairs (user 2026-11-01:
 * only the app pairs; neither app nor firmware holds the derivation). No PIN stored: no advertising.
 * Just Works is refused. No USB power gate since 2026-11-01: the PIN proves the phone; on battery a
 * board without a network advertises in windows (provision.c).
 *
 *   service  4c43a000-4c45-4b43-414d-000000000001
 *   SSID     ...0002  write, authen.      Wi-Fi SSID (UTF-8, <= 32 bytes)
 *   PSK      ...0003  write, authen.      Wi-Fi passphrase (8..63 bytes)
 *   COMMIT   ...0004  write, authen.      1 byte: 0x01 check, then store if the check passed;
 *                                          0x02 store without checking (set up away from the router);
 *                                          + 4 bytes: the phone's attempt id (little-endian 32 bit)
 *   STATUS   ...0005  read, notify,        7 bytes: state, detail (LE16), attempt id (LE32): the last
 *                     authenticated        commit's state, kept across connections. States in
 *                                          wifi_check_calc.h: progress (joining, address, internet), then
 *                                          saved or the failure. STATUS is the state: a notification or
 *                                          the link can be lost, so the phone also reads it and, after a
 *                                          lost link, reconnects and reads it for its attempt id
 *
 * The check (wifi_check.h) joins the network, gets an address by DHCP and fetches a well-known
 * URL from the BL616 itself, so a wrong password, a network out of reach and a network without
 * internet are all told apart before anything is stored. It needs the radio and takes it even from a
 * running K230 session (BLE first, user 2026-10-28): the K230 hears a link loss and gets the radio
 * back after the check; no K230 session starts during it. New credentials stored while a K230
 * session runs end that session; a new one starts on them (ble_credentials_stored()).
 *
 * The credentials live on the BL616 because it is the Wi-Fi device (SDIO Wi-Fi for the K230).
 */
#ifndef LEAKCAM_BLE_PAIRING_H
#define LEAKCAM_BLE_PAIRING_H

#include <stdbool.h>

/* Initialise the BLE controller and host and register the service (RF must be up); advertising follows
 * ble_pairing_advertise(). Must run inside a FreeRTOS task (the host calls back from its own tasks). */
void ble_pairing_start(void);
/* advertise or not (provision.c: always on USB power, on battery in windows while no network is stored or presence
 * is lost); a held connection stays, advertising resumes after it as wanted */
void ble_pairing_advertise(bool on);
/* the advertising interval in ms (20 .. 10240); the product keeps its 100-150 ms, the test firmware sweeps it
 * (tests/test114): a shared-module function only the test firmware calls, dropped from the product by the linker */
void ble_pairing_adv_interval(unsigned int ms);
/* the stored pairing PIN, 0..999999; -1 when none (`prov pin` stores it) */
int ble_pairing_pin(void);
/* a phone is connected (presence.c waits: setup first) */
bool ble_pairing_connected(void);
/* Called after new credentials are stored, from the BLE host or the check's task (must not block). The product
 * (provision.c) drops the remembered AP; the test firmware (pt_wifi.c) also restarts its bridge. */
void ble_credentials_stored(void);

#endif
