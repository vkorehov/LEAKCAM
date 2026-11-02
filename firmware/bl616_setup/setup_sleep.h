/*
 * The setup firmware's sleep (DESIGN.md 5.3.1, tests/test120 and test121): PDS between BLE advertisements with the SDK's
 * low-power firmware (CONFIG_LPAPP, accurate tickless). Rules and obstacles: skill bl616-ble-low-power.
 */
#ifndef LEAKCAM_SETUP_SLEEP_H
#define LEAKCAM_SETUP_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

/* the low-power callbacks and the USB plug wake; after bl_lp_init() (setup_boot.c) */
void setup_sleep_init(void);
/* the phone connection callbacks (a connection keeps the chip awake); after ble_pairing_start() */
void setup_sleep_ble_register(void);

/* sleep between advertisements until setup_sleep_end(); refused while USB power is present (the full firmware's
 * case) unless usb_too (only the bench's current measurement through USB-C 1). The USB console is detached for the
 * whole sleep and the watchdog's feeding held */
void setup_sleep_begin(bool usb_too);
/* a phone connected (true: awake, console back) or left (false: asleep again if the sleep still runs) */
void setup_sleep_pause(bool pause);
void setup_sleep_end(void);

/* BQ24072 PGOOD low: USB-C 1 has power */
bool setup_usb_present(void);
/* given by the firmware: USB-C 1 was plugged in during the sleep (task context, the sleep has ended). The setup
 * firmware switches to the full firmware there (slot switch); the test firmware reports it */
void setup_usb_plugged(void);

struct setup_sleep_stats {
    uint32_t calls;         /* idle hook calls that went to the SDK's sleep path */
    uint32_t short_idle;    /* of those, idle time too short to sleep */
    uint32_t max_idle;      /* the longest idle time offered, ms */
    uint32_t entered;       /* sleeps that reached the low-power firmware */
    uint32_t exited;        /* of those, wakes through lp_exit (the clocks, the tick and the watchdog restarted) */
    uint32_t wdg_enter_max; /* the watchdog counter at a sleep's start, highest (it restarts at every wake) */
    uint32_t wdg_exit_max;  /* the counter right after a wake's restart, highest (0 or near) */
    uint32_t vetoed;        /* idle hook calls kept out of the sleep path: BLE up but idle */
    uint32_t usb_wakes;     /* wakes by the USB plug */
};
void setup_sleep_stats(struct setup_sleep_stats *s);

#endif
