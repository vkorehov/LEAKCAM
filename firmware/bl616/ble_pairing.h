/*
 * BLE on USB power, used for one thing only: setting the Wi-Fi credentials (and checking them
 * before they are stored).
 *
 * The device advertises as "LEAKCAM-xxyy" and exposes one provisioning service. Its characteristics
 * need an encrypted link, so a phone must pair first. The board has no display or buttons, so
 * pairing is LE Secure Connections "Just Works"; physical access is the gate instead: pairing
 * requests are only accepted while USB power is present (usb_present()).
 *
 *   service  4c43a000-4c45-4b43-414d-000000000001
 *   SSID     ...0002  write, encrypted     Wi-Fi SSID (UTF-8, <= 32 bytes)
 *   PSK      ...0003  write, encrypted     Wi-Fi passphrase (8..63 bytes)
 *   COMMIT   ...0004  write, encrypted     1 byte: 0x01 check, then store if the check passed;
 *                                          0x02 store without checking (set up away from the router);
 *                                          + 4 bytes: the phone's attempt id (little-endian 32 bit)
 *   STATUS   ...0005  read, notify,        7 bytes: state, detail (LE16), attempt id (LE32): the last
 *                     encrypted            commit's state, kept across connections. States in
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

/* Initialise RF, the BLE controller and host, register the service and start advertising.
 * Must run inside a FreeRTOS task (the host calls back from its own tasks). */
void ble_pairing_start(void);
/* Stop advertising and drop any connection (called when USB is unplugged). */
void ble_pairing_stop(void);
/* a phone is connected (presence.c waits: setup first) */
bool ble_pairing_connected(void);
/* Called after new credentials are stored, from the BLE host or the check's task (must not block). The product
 * (provision.c) drops the remembered AP; the test firmware (pt_wifi.c) also restarts its bridge. */
void ble_credentials_stored(void);

#endif
