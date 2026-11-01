/*
 * Use case 1: Wi-Fi provisioning from the phone (tests/test040, tests/test106). BLE from boot; the BL616 advertises
 * the provisioning service (ble_pairing.c) always on USB power and, on battery, 60 s every 5 minutes while no network
 * is stored; the phone pairs with the board's pre-stored PIN, sends a network, the BL616 checks it (wifi_check.c)
 * and stores it in easyflash.
 */
#ifndef LEAKCAM_PROVISION_H
#define LEAKCAM_PROVISION_H

void provision_start(void);           /* the supervisor task: storage, RF, presence, BLE, advertising windows */

#endif
