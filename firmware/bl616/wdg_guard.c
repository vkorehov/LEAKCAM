#include "wdg_guard.h"

#include "bflb_wdg.h"
#include "bl616_glb.h"
#include "bl616_hbn.h"
#include "FreeRTOS.h"
#include "task.h"

static struct bflb_device_s *wdg;

static void wdg_feed_task(void *arg)
{
    (void)arg;
    while (1) {
        bflb_wdg_reset_countervalue(wdg);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool wdg_guard_start(void)
{
    GLB_RESET_RECORD_Type rr;
    GLB_Get_Reset_Reason(&rr);
    GLB_Clr_Reset_Reason();
    struct bflb_wdg_config_s cfg = {
        .clock_source = WDG_CLKSRC_32K,
        .clock_div = 31,                    /* 32768 / 32: ~1 kHz */
        .comp_val = WDG_GUARD_MS,
        .mode = WDG_MODE_RESET,
    };
    wdg = bflb_device_get_by_name("watchdog0");
    bflb_wdg_init(wdg, &cfg);
    bflb_wdg_start(wdg);
    xTaskCreate(wdg_feed_task, "wdg", 256, NULL, configMAX_PRIORITIES - 1, NULL);
    return rr.reset_recorder_wdt_rst_n;
}

#ifdef CONFIG_SHELL
#include "shell.h"

extern int bl_sys_reset_por(void);

/* ISP entry for tools/power-bringup/bl616_isp.py: the SDK's `reboot uart` sets the ROM's user-boot field and resets
 * with bl_sys_reset_por(), which leaves this watchdog running: it reset the ROM every 8 s, before the ROM had
 * enumerated on USB, and the field (kept across resets) sent every reset back into the ROM - no USB device until a
 * 3V3 cut (2026-10-23, tests/test100 Obstacles). Stop the watchdog first, then the same as `reboot uart`. */
static void cmd_isp(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bflb_wdg_stop(wdg);
    HBN_Set_User_Boot_Config(1);
    bl_sys_reset_por();
}
SHELL_CMD_EXPORT_ALIAS(cmd_isp, isp, watchdog off then reset into the ROM for USB ISP);
#endif
