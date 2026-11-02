#include "wdg_guard.h"

#include "bflb_mtimer.h"
#include "bflb_wdg.h"
#include "bflb_clock.h"
#include <stdio.h>
#include <string.h>
#include "bl616_glb.h"
#include "bl616_hbn.h"
#include "FreeRTOS.h"
#include "task.h"
#include "crash_rec.h"

static struct bflb_device_s *wdg;
static TaskHandle_t feed_task;
static volatile bool feed_held;
static uint8_t reset_record;
static const struct bflb_wdg_config_s cfg = {
    .clock_source = WDG_CLKSRC_32K,
    .clock_div = 31,                        /* 32768 / 32: ~1 kHz */
    .comp_val = WDG_GUARD_MS,
    .mode = WDG_MODE_RESET,
};

static void wdg_feed_task(void *arg)
{
    (void)arg;
    while (1) {
        if (feed_held)
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bflb_wdg_reset_countervalue(wdg);
        crash_rec_fed();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool wdg_guard_start(void)
{
    GLB_RESET_RECORD_Type rr;
    if (GLB_Get_Reset_Reason(&rr) != SUCCESS || GLB_Clr_Reset_Reason() != SUCCESS)
        vAssertCalled();
    memcpy(&reset_record, &rr, 1);          /* the eight record bits, tests/test124 */
    /* the watchdog's bus clock: a low-power build's board_init() gates it (peripheral_clock_init_lp(), tests/test120) */
    PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
    wdg = bflb_device_get_by_name("watchdog0");
    bflb_wdg_init(wdg, &cfg);
    bflb_wdg_start(wdg);
    if (xTaskCreate(wdg_feed_task, "wdg", 256, NULL, configMAX_PRIORITIES - 1, &feed_task) != pdPASS)
        vAssertCalled();
    return rr.reset_recorder_wdt_rst_n;
}

uint8_t wdg_guard_reset_record(void)
{
    return reset_record;
}

/* setup mode (tests/test120): no feeding while the chip sleeps in PDS between radio events; stopped before the sleep,
 * started again (counter reset) after the wake. The crash recorder's hang detector restarts with it: the feeder is held
 * through the sleep window, and without this it took every window for a hang; an app hung while awake is still caught
 * by both (tests/test124, 2026-11-02). After a PDS wake both again, its bus clock and its whole setup: the SDK's wake
 * (board_recovery(), peripheral_clock_init_lp()) rewrites the clock gates without the timer's, after the firmware's
 * own wake code, so a started watchdog stood still (counter 3 -> 3, enabled) and a hang after a sleep window was never
 * reset, only EN ended it; and PDS loses its clock source and divider, so with the clock alone it counted fast and
 * reset the chip a few seconds into every sleep window (tests/test124, 2026-11-02) */
void wdg_guard_pause(bool pause)
{
    if (pause) {
        bflb_wdg_stop(wdg);
    } else {
        PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
        bflb_wdg_init(wdg, &cfg);
        bflb_wdg_reset_countervalue(wdg);
        bflb_wdg_start(wdg);
        crash_rec_fed();
    }
}

/* setup mode (tests/test120): the feed task's 1 s period woke the chip from PDS twice per 2 s advertising interval
 * (180 wakes in 120 s, 2026-11-01). Held for the sleep window: each wake restarts the counter (wdg_guard_pause), so an
 * app hung while awake is still reset after WDG_GUARD_MS. */
void wdg_guard_feed_hold(bool hold)
{
    feed_held = hold;
    if (!hold)
        xTaskNotifyGive(feed_task);
}

#ifdef CONFIG_SHELL
#include "shell.h"
#include "usbd_core.h"

extern int bl_sys_reset_por(void);

/* ISP entry for tools/power-bringup/bl616_isp.py: the SDK's `reboot uart` sets the ROM's user-boot field and resets
 * with bl_sys_reset_por(), which leaves this watchdog running: it reset the ROM every 8 s, before the ROM had
 * enumerated on USB, and the field (kept across resets) sent every reset back into the ROM - no USB device until a
 * 3V3 cut (2026-10-23, tests/test100 Obstacles). Stop the watchdog first, then the same as `reboot uart`.
 * And detach from USB first: 2 of 7 ROM entries that day never reached the host, with the watchdog off too. The app
 * vanished and the ROM attached a few ms later, inside the host's ~100 ms connect debounce; a clean unplug (D+ off,
 * PHY down: usbd_deinitialize) and 500 ms before the reset make the ROM a new attach. */
static void cmd_isp(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bflb_wdg_stop(wdg);
    printf("isp: USB detach, the ROM in 0.5 s\r\n");
    bflb_mtimer_delay_ms(20);
    (void)usbd_deinitialize(0);             /* the console's bus (usb_console2.c USB_CONSOLE_BUS_ID); the ROM is
                                             * entered either way */
    bflb_mtimer_delay_ms(500);
    if (HBN_Set_User_Boot_Config(1) != SUCCESS)
        vAssertCalled();
    (void)bl_sys_reset_por();               /* never returns */
}
SHELL_CMD_EXPORT_ALIAS(cmd_isp, isp, watchdog off then reset into the ROM for USB ISP);
#endif
