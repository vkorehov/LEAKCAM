/*
 * The provisioning of a board's Wi-Fi, without its transport: BLE (ble_pairing.c, the GATT characteristics) and the USB
 * cable (usb_setup.c, lines through the K230) both drive it (DESIGN.md 5.3.4, 5.3.8). The phone writes SSID and
 * passphrase, commits with its attempt id; the check (wifi_check.c, up to the LEAKCAM API) runs in its own task and
 * STATUS follows it: state, detail, attempt. Credentials are never printed.
 */
#ifndef LEAKCAM_PROVISION_H
#define LEAKCAM_PROVISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROVISION_STATUS_LEN 7   /* state, detail LE16, attempt LE32 */

/* a new session (a phone connected): nothing written yet; a running check goes on */
void provision_clear(void);
/* 0, or -1: too long (SSID 32, passphrase 63) or a passphrase of 1..7 characters (WPA2: 8..63; empty = open) */
int provision_ssid(const char *s, size_t n);
int provision_psk(const char *k, size_t n);
/* 1 = check then store, 2 = store as is; attempt = the phone's id, STATUS answers for it. 0, or -1: no SSID written or
 * an unknown command. During a check: 0, the running check reports */
int provision_commit(uint8_t cmd, uint32_t attempt);
void provision_status(uint8_t out[PROVISION_STATUS_LEN]);
bool provision_checking(void);
/* called on every STATUS change (from the check's task or the committing one); up to 2 listeners */
void provision_on_status(void (*cb)(const uint8_t st[PROVISION_STATUS_LEN]));
/* given by a transport that must finish something before the check claims the radio (BLE: its Wi-Fi warm-up); runs in
 * the check's task */
void provision_before_check(void);

#endif
