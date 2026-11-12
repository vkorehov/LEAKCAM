/*
 * The full firmware on USB power (DESIGN.md 5.3.11, user 2026-11-06): the K230 on the whole time, with WAKE `usb`,
 * relaying the cable as in the setup firmware, so the app reaches the board over USB in either firmware; the battery
 * read by the running K230 the moment USB power goes, then the K230 off.
 */
#ifndef LEAKCAM_USB_FULL_H
#define LEAKCAM_USB_FULL_H

#include "k230_link.h"

/* once at boot, after the radio (easyflash): the USB watch task */
void usb_full_start(void);

/* every K230 session's commands in the full firmware (usb, leak, rtc): the cable's lines and the leak check's DONE */
void usb_full_cmd(const struct link_msg *m);

/* the session ends now and comes back at once, without the RETRY_MS hold of a failed one (capture.c: the Wi-Fi bridge
 * off after a capture) */
void usb_full_again(void);

#endif
