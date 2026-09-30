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
 *                                          0x02 store without checking (set up away from the router)
 *   STATUS   ...0005  read, notify,        3 bytes: state, detail (little-endian 16 bit), states in
 *                     encrypted            wifi_check_calc.h: progress (joining, address, internet),
 *                                          then saved or the failure
 *
 * The check (wifi_check.h) joins the network, gets an address by DHCP and fetches a well-known
 * URL from the BL616 itself, so a wrong password, a network out of reach and a network without
 * internet are all told apart before anything is stored. It needs the radio, so it is refused
 * (BUSY) while a K230 session runs, and no K230 session starts during it.
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

#endif
