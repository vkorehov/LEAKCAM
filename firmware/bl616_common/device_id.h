/*
 * Use case 2: the device's identity on every channel (USB serial number, BLE advertising, server check-ins): the
 * BL616's factory MAC from eFuse, as 12 upper-case hex digits in the order the router shows it (tests/test111).
 */
#ifndef LEAKCAM_DEVICE_ID_H
#define LEAKCAM_DEVICE_ID_H

#include <stdint.h>

const char *device_id(void);                 /* "7C3E8262B149" */
void device_id_bytes(uint8_t mac[6]);        /* the same, as bytes (BLE manufacturer data) */

#endif
