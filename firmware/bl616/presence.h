/*
 * Use case 2: presence on USB power (tests/test111). Every poll interval (the server's "poll", 60 s until it says
 * otherwise) the BL616 joins the stored network and checks in at the server with its device ID
 * (GET /v1/poll?id=<id>), the same run as the BLE credential check with the server as its target (wifi_check.c). The
 * app asks the server which of its LEAKCAMs are online. Never while a phone is connected (setup first). On battery: a
 * later use case (the HBN scheduler, tests/test108).
 */
#ifndef LEAKCAM_PRESENCE_H
#define LEAKCAM_PRESENCE_H

void presence_start(void);                   /* after radio_init(): easyflash and RF are up */

#endif
