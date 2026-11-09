/*
 * The setup firmware's start (DESIGN.md 5.3.1): the low-power framework, RF, Wi-Fi and the product's BLE, in the order
 * proven on rev 1 (tests/test120; skill bl616-ble-low-power). Runs in a task after the scheduler: the USB console is
 * up first, so a hang in a step still leaves `make flash` working.
 */
#include "setup_boot.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "bflb_boot2.h"
#include "bl_lp.h"
#include "bl616_hbn.h"
#include "bl616_pm.h"
#include "clock_manager.h"
#include "easyflash.h"
#include "pm_manager.h"
#include "rfparam_adapter.h"

#include "ble_pairing.h"
#include "blecore.h"
#include "evlog.h"
#include "setup_sleep.h"
#include "wifi_setup.h"

/* the product's BLE: advertising as the device ID with the provisioning service, PIN pairing; every 2 s while the chip
 * sleeps (test114: the phone finds it every time; test120: 10/10 while it sleeps), every 100 ms while it is awake
 * anyway (USB power) or a phone waits (its wake). The phone connects at an advertisement it catches and gives up after
 * 5 s: at 2 s a setup takes 16-23 s now and then, without a dialog since the app bonds with every connect (test106,
 * 2026-11-02; user: 2 s kept on battery). Started during the Wi-Fi start's pause, as the SDK example orders it.
 * app_clock_init() first: the stored RC32K calibration */
/* The BLE core wedge's other form (bug-006, skill bl616-ble-low-power 5): after a reset the controller's init spins
 * for good in rwip_time_get. The start takes well under a second; not done in BLE_START_DEADLINE_MS -> EV_WEDGE 2
 * (the timer task outranks the spinning start task). */
#define BLE_START_DEADLINE_MS 5000

static void ble_start_late(TimerHandle_t t)
{
    (void)t;
    evlog_add(EV_WEDGE, 2, 0);
    blecore_dump(BLECORE_STUCK);
    printf("setup: BLE start not done in %u ms: the BLE core wedge, only EN or a power loss clears it\r\n",
           BLE_START_DEADLINE_MS);
}

static void ble_start_task(void *arg)
{
    TimerHandle_t late = xTimerCreate("ble_late", pdMS_TO_TICKS(BLE_START_DEADLINE_MS), pdFALSE, NULL, ble_start_late);
    if (late == NULL || xTimerStart(late, 0) != pdPASS)
        vAssertCalled();
    app_clock_init();
    ble_pairing_start();
    setup_sleep_ble_register();
    ble_pairing_adv_interval((unsigned int)(uintptr_t)arg);
    ble_pairing_advertise(true);
    (void)xTimerDelete(late, 0);
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
    char why[8] = { 0 };
    size_t len = 0;
    (void)ef_get_env_blob("setup_why", why, sizeof(why) - 1, &len);   /* read, not deleted: switch_to_full_start() */
    uintptr_t adv_ms = setup_usb_present() || !strcmp(why, "wake") ? SETUP_ADV_FAST_MS : SETUP_ADV_SLEEP_MS;
    /* no PIN (a board not yet provisioned): no advertising, so no BLE at all; the chip sleeps instead of staying
     * awake on the cell (bugs/bug-006 follow-up). The PIN is stored over USB, then a reboot */
    if (ble_pairing_pin() < 0)
        ble_pairing_never();
    else if (xTaskCreate(ble_start_task, "ble_start", 1024, (void *)adv_ms, 15, NULL) != pdPASS)
        vAssertCalled();
}
