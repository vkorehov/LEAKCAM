/*
 * The full firmware on USB power (DESIGN.md 5.3.11, user 2026-11-06): the K230 on the whole time, with WAKE `usb`,
 * relaying the cable as in the setup firmware, so the app reaches the board over USB in either firmware; the battery
 * read by the running K230 the moment USB power goes, then the K230 off.
 */
#ifndef LEAKCAM_USB_FULL_H
#define LEAKCAM_USB_FULL_H

/* once at boot, after the radio (easyflash): the USB watch task */
void usb_full_start(void);

#endif
