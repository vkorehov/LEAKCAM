/*
 * The setup firmware's start (setup_boot.c).
 */
#ifndef LEAKCAM_SETUP_BOOT_H
#define LEAKCAM_SETUP_BOOT_H

#define SETUP_ADV_FAST_MS  100     /* advertising while awake (USB power) or after a phone's wake */
#define SETUP_ADV_SLEEP_MS 2000    /* advertising while the chip sleeps on battery */

/* the low-power framework, RF, Wi-Fi and BLE; from a task (it waits on flash and RF). BLE comes up by itself after it */
void setup_boot(void);

#endif
