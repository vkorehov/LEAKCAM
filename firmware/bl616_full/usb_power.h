/*
 * USB input power detection through the charger's PGOOD (BQ24072 U1.7, open drain, R23 100k to
 * 3V3_SLEEP). PGOOD is low while the USB input is valid. While USB powers the board the charger's
 * power path supplies VBAT (OUT) from USB, so the BL616 can stay awake without draining the cell.
 */
#ifndef LEAKCAM_USB_POWER_H
#define LEAKCAM_USB_POWER_H

#include <stdbool.h>
#include <stdint.h>

void usb_sense_init(void);           /* IO03 analog, PGOOD on ACOMP0 (tests/test001, test035) */
bool usb_present(void);              /* PGOOD low: USB input valid */

#endif
