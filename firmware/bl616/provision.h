/*
 * Use case 1: Wi-Fi provisioning from the phone (tests/test040, tests/test106). While USB powers the board the
 * BL616 advertises the provisioning service (ble_pairing.c); the phone sends a network, the BL616 checks it
 * (wifi_check.c) and stores it in easyflash. No USB, no BLE.
 */
#ifndef LEAKCAM_PROVISION_H
#define LEAKCAM_PROVISION_H

void provision_start(void);           /* the supervisor task: BLE on while USB is present */

#endif
