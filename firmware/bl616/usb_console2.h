/*
 * Second CDC ACM port of the LEAKCAM USB console (usb_console2.c): the K230's console, bridged over the
 * K230 <-> BL616 UART. Port 0 keeps the SDK's usb_console_write() (printf, the BL616 shell).
 */
#ifndef LEAKCAM_USB_CONSOLE2_H
#define LEAKCAM_USB_CONSOLE2_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* to the host on port 1 (/dev/ttyACM1); dropped while no host is attached or the ring is full */
ssize_t usb_k230_write(const void *data, size_t size);

/* what the host typed on port 1; called from the USB console task. Weak: a firmware without the bridge ignores it */
void usb_k230_rx(const uint8_t *data, uint32_t len);

/* around PDS sleep (setup mode, tests/test120): the USB device detached before, attached again after the wake */
void usb_console_suspend(void);
void usb_console_resume(void);

#endif
