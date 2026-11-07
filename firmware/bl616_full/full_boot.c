#include "full_boot.h"

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_boot2.h"
#include "easyflash.h"
#include "rfparam_adapter.h"

#include "k230_session.h"
#include "leak_check.h"
#include "presence.h"
#include "switch_to_setup.h"
#include "usb_full.h"
#include "wifi_link.h"

/* the partition table, easyflash and the RF calibration once per boot, in this order: rfparam reads its calibration
 * through the partition table, and a second easyflash_init() leaves littlefs unusable (tests/test040). Any failure is
 * a broken flash: fail fast (bflb_mtd_init() would drop the partition table's result) */
static void radio_init(void)
{
    if (bflb_boot2_init() != 0 || easyflash_init() != EF_NO_ERR || rfparam_init(0, NULL, 0) != 0)
        vAssertCalled();
}

static void boot_task(void *arg)
{
    (void)arg;
    radio_init();
    if (!wifi_link_has_credentials())
        switch_to_setup(NULL);
    presence_start();                       /* checks in whenever a network is stored (use case 2) */
    k230_session_init();
    usb_full_start();                       /* on USB power the K230 on the whole time (DESIGN.md 5.3.11) */
    leak_check_start();                     /* the probe and the RTC check (DESIGN.md 5.3.12) */
    vTaskDelete(NULL);
}

void full_boot_start(void)
{
    if (xTaskCreate(boot_task, "boot", 1536, NULL, 3, NULL) != pdPASS)
        vAssertCalled();
}
