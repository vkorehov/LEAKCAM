/*
 * Use case 2: presence (tests/test111). From boot, on USB and on battery, every poll interval (the server's "poll",
 * 60 s until it says otherwise) the BL616 joins the stored network and checks in at the server with its device ID
 * and the stored network's SSID (GET /v1/poll?id=<id>&ssid=<ssid>, for the app; never the passphrase), the same run
 * as the BLE credential check with the server as its target (wifi_check.c); no stored network, no check-in; no
 * internet, the next interval tries again. The app asks the server which of its LEAKCAMs are online. Never while a
 * phone is connected (setup first). On battery it stays awake meanwhile: the HBN scheduler that sleeps between
 * check-ins is tests/test108.
 */
#ifndef LEAKCAM_PRESENCE_H
#define LEAKCAM_PRESENCE_H

void presence_start(void);                   /* after radio_init(): easyflash and RF are up */

#endif
