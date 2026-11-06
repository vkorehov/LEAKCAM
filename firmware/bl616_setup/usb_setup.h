/*
 * Setup over the USB cable (usb_setup.c): the phone app on USB-C 1, through the K230's USB relay.
 */
#ifndef LEAKCAM_USB_SETUP_H
#define LEAKCAM_USB_SETUP_H

#include <stdbool.h>

/* once at boot, before any session */
void usb_setup_init(void);
/* USB power and the setup firmware stays: the K230 on with WAKE setup (no PIN stored: nothing) */
void usb_setup_begin(void);
/* the cable pulled, or the network saved: the K230 off */
void usb_setup_end(const char *why);
bool usb_setup_active(void);

#endif
