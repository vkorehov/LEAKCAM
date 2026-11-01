/*
 * The setup firmware's start (setup_boot.c).
 */
#ifndef LEAKCAM_SETUP_BOOT_H
#define LEAKCAM_SETUP_BOOT_H

/* the low-power framework, RF, Wi-Fi and BLE; from a task (it waits on flash and RF). BLE comes up by itself after it */
void setup_boot(void);

#endif
