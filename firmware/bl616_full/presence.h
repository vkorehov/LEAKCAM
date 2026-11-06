/*
 * Use case 2: presence (tests/test111). From boot, on USB and on battery, every poll interval (the server's "poll",
 * 60 s until it says otherwise) the BL616 joins the stored network and checks in at the server with its device ID
 * and the stored network's SSID (GET /v1/poll?id=<id>&ssid=<ssid>, for the app; never the passphrase), the same run
 * as the setup firmware's credential check with the server as its target (wifi_check.c). The app asks the server
 * which of its LEAKCAMs are online. On battery it stays awake meanwhile: the HBN scheduler that sleeps between
 * check-ins is tests/test108. A check-in that did not reach the server, for whatever reason (the network gone, a
 * wrong password, no internet, the server down), is presence lost: the board switches to the setup firmware, where a
 * phone nearby sets the Wi-Fi (tests/test117). A phone out of range wakes a present board through the server: the
 * check-in answer carries "ble":1 and the board switches to the setup firmware too (tests/test119). No BLE here.
 */
#ifndef LEAKCAM_PRESENCE_H
#define LEAKCAM_PRESENCE_H

void presence_start(void);                   /* after radio_init(): easyflash and RF are up */
/* check in now, not at the end of the interval: the battery read at the unplug reaches the server and the app at once
 * (usb_full.c, DESIGN.md 5.3.11) */
void presence_now(void);

#endif
