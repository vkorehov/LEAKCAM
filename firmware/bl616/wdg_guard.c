#include "wdg_guard.h"

#include "bflb_wdg.h"
#include "bl616_glb.h"
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
