#include "full_boot.h"

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_boot2.h"
#include "easyflash.h"
#include "thresholds.h"
#include "rfparam_adapter.h"

#include "climate.h"
#include "hbn_sleep.h"
#include "k230_power.h"
#include "k230_session.h"
#include "leak_check.h"
#include "presence.h"
#include "switch_to_setup.h"
#include "usb_full.h"
#include "usb_power.h"
#include "wifi_link.h"

/* the partition table, easyflash and the RF calibration once per boot, in this order: rfparam reads its calibration
 * through the partition table, and a second easyflash_init() leaves littlefs unusable (tests/test040). Any failure is
 * a broken flash: fail fast (bflb_mtd_init() would drop the partition table's result) */
static void radio_init(void)
{
    if (bflb_boot2_init() != 0 || easyflash_init() != EF_NO_ERR || rfparam_init(0, NULL, 0) != 0)
        vAssertCalled();
    thresholds_load();                      /* the server's leak thresholds, before the probe and the climate use them */
}

/* the board's heat sources (test048): the K230 and the charger on USB power */
static bool warm(void)
{
    return k230_power_is_on() || usb_present();
}

/* 85 %RH or more (DESIGN.md 5.3.15): the server sees it at once (rh= in the check-in) and the K230 checks the scene */
static void humid(int rh_x10)
{
    (void)rh_x10;
    presence_now();
    leak_check_now("humid");
}

static void boot_task(void *arg)
{
    (void)arg;
    radio_init();
    if (!wifi_link_has_credentials())
        switch_to_setup(NULL);
    k230_session_init();
    usb_full_start();                       /* on USB power the K230 on the whole time (DESIGN.md 5.3.11) */
    leak_check_start();                     /* the probe and the RTC check (DESIGN.md 5.3.12) */
    /* the AHT20 every 10 min: the room, the 85 %RH alarm (DESIGN.md 5.3.15); before the radio: a wake's first thing */
    climate_start(warm, humid, hbn_sleep_woke() != 0);
    presence_start();                       /* checks in whenever a network is stored (use case 2) */
    hbn_sleep_start();                      /* on battery HBN between the events (DESIGN.md 5.3.12) */
    vTaskDelete(NULL);
}

void full_boot_start(void)
{
    if (xTaskCreate(boot_task, "boot", 1536, NULL, 3, NULL) != pdPASS)
        vAssertCalled();
}
