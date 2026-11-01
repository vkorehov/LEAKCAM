/*
 * The setup firmware's start (DESIGN.md 5.3.1): the low-power framework, RF, Wi-Fi and the product's BLE, in the order
 * proven on rev 1 (tests/test120; skill bl616-ble-low-power). Runs in a task after the scheduler: the USB console is
 * up first, so a hang in a step still leaves `make flash` working.
 */
#include "setup_boot.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bflb_boot2.h"
#include "bl_lp.h"
#include "bl616_hbn.h"
#include "bl616_pm.h"
#include "clock_manager.h"
#include "easyflash.h"
#include "pm_manager.h"
#include "rfparam_adapter.h"

#include "ble_pairing.h"
#include "setup_sleep.h"
#include "wifi_setup.h"

/* the product's BLE: advertising as the device ID with the provisioning service, PIN pairing, every 2 s (setup mode,
 * test114: the phone finds it every time; test120: 10/10 while the chip sleeps). Started during the Wi-Fi start's
 * pause, as the SDK example orders it. app_clock_init() first: the stored RC32K calibration */
static void ble_start_task(void *arg)
{
    (void)arg;
    app_clock_init();
    ble_pairing_start();
    setup_sleep_ble_register();
    ble_pairing_adv_interval(2000);
    ble_pairing_advertise(true);
    vTaskDelete(NULL);
}

void setup_boot(void)
{
    hal_pm_ldo11_cfg(PM_PDS_LDO_LEVEL_SOC_DEFAULT, PM_PDS_LDO_LEVEL_RT_DEFAULT, PM_PDS_LDO_LEVEL_AON_DEFAULT);
    HBN_Enable_RTC_Counter();
    pm_rc32k_auto_cal_init();

    if (bflb_boot2_init() != 0 || easyflash_init() != EF_NO_ERR)
        vAssertCalled();            /* the partition table and easyflash: nothing works without them */

    /* RC32K. The example stores the passive crystal (Y3) and resets the chip, and app_clock_init() falls back to RC and
     * stores that when its crystal check fails: a possible reset loop with a flash write per boot. Y3 needs the HBN
     * pulls off and a start-up wait (test032). A stored crystal setting is replaced once (one reset). */
    if (app_set_clock_source(CLOCK_SOURCE_RC) != 0)
        vAssertCalled();

    pm_sys_init();
    bl_lp_init();
    setup_sleep_init();

    extern int bl_sys_em_config(void);
    bl_sys_em_config();             /* the BLE controller's exchange memory */

    if (rfparam_init(0, NULL, 0) != 0)
        vAssertCalled();            /* no radio: nothing to set up with */

    wifi_setup_start();
    if (xTaskCreate(ble_start_task, "ble_start", 1024, NULL, 15, NULL) != pdPASS)
        vAssertCalled();
}
