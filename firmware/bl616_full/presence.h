/*
 * Use case 2: presence (tests/test111). From boot, on USB and on battery, every poll interval (the server's "poll",
 * 60 s until it says otherwise) the BL616 joins the stored network and checks in at the server with its device ID
 * and the stored network's SSID (GET /v1/poll?id=<id>&ssid=<ssid>, for the app; never the passphrase), the same run
 * as the BLE credential check with the server as its target (wifi_check.c); no stored network, no check-in; no
 * internet, the next interval tries again. The app asks the server which of its LEAKCAMs are online. Never while a
 * phone is connected (setup first). On battery it stays awake meanwhile: the HBN scheduler that sleeps between
 * check-ins is tests/test108. A check-in that did not reach the server, for whatever reason (the network gone, a
 * wrong password stored without checking, no internet, the server down), is presence lost: BLE advertises again
 * (provision.c) so a phone nearby can set the Wi-Fi at once (tests/test117). A phone out of range wakes a present
 * board through the server: the check-in answer carries "ble":1, the board advertises for 2 minutes (tests/test119).
 */
#ifndef LEAKCAM_PRESENCE_H
#define LEAKCAM_PRESENCE_H

#include <stdbool.h>
#include <stdint.h>

void presence_start(void);                   /* after radio_init(): easyflash and RF are up */
void presence_now(void);                     /* check in now, not at the next interval (new credentials) */
bool presence_lost(void);                    /* the last check-in did not reach the server */
uint32_t presence_woken_ms(void);            /* when the server last asked for BLE ("ble":1, a phone's wake), 0 never */

#endif
