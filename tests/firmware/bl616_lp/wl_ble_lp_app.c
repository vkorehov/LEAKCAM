#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "mm.h"

#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "bl_fw_api.h"
#include "wifi_mgmr_ext.h"
#include "wifi_mgmr.h"

#include "bflb_irq.h"
#include "bflb_mtimer.h"
#include "board.h"
#include "bl_lp.h"
#if defined(BL618DG)
#include "bl618dg_pm.h"
#include "bl618dg_glb.h"
#include "bl618dg_hbn.h"
#else
#include "bl616_pm.h"
#include "bl616_glb.h"
#include "bl616_glb_gpio.h"
#include "bl616_hbn.h"
#endif
#include "bflb_uart.h"
#include "bflb_gpio.h"
#include "bflb_clock.h"
#include "bflb_rtc.h"
#include "shell.h"
#include "pm_manager.h"
#include "wdg_guard.h"

int pwr_info_clear(void)
{
#if defined(BL618DG)
    if (iot2lp_para->lp_info != NULL) {
        bl_lp_info_clear(iot2lp_para->lp_info);
    }
#else
    bl_lp_info_clear();
#endif

    return 0;
}

#define SLEEP_PDS_US   80
#define ACTIVE_LPFW_US 38000
#define ACTIVE_APP_US  57000

/* LEAKCAM: the window's figures kept for `lp_pwr`: printed at the window's end they went out before the USB console
 * was back (test120) */
static bl_lp_info_t lp_info;

uint64_t pwr_info_get(void)
{
    printf("Power info dump:\r\n");
    printf("LPFW try recv bcn: %d, loss %d\r\n", lp_info.lpfw_recv_cnt, lp_info.lpfw_loss_cnt);
#if defined(BL618DG)   /* the BL616 lp info has no lpfw advertising counters: its BLE sleeps in the controller */
    printf("LPFW try ble adv: %u, loss %u\r\n", lp_info.ble_lpfw_adv_success_cnt, lp_info.ble_lpfw_adv_loss_cnt);
#endif
    printf("Total time %lldms\r\n", lp_info.time_total_us / 1000);
    printf("PDS sleep: %lldms\r\n", lp_info.sleep_pds_us / 1000);
    printf("LPFW active: %lldms\r\n", lp_info.active_lpfw_us / 1000);
    printf("APP active: %lldms\r\n", lp_info.active_app_us / 1000);

    uint64_t current = (lp_info.sleep_pds_us * SLEEP_PDS_US + lp_info.active_lpfw_us * ACTIVE_LPFW_US +
                        lp_info.active_app_us * ACTIVE_APP_US) /
                       lp_info.time_total_us;

    printf("Predict current: %llduA\r\n", current);

    return current;
}

extern void lp_window_begin(void);
extern void lp_window_end(void);

void timerCallback(TimerHandle_t xTimer)
{
    bl_lp_info_get(&lp_info);
    lp_window_end();                /* LEAKCAM: main.c, the USB console and watchdog feeding after the window */
    xTimerDelete(xTimer, portMAX_DELAY);
}

void createAndStartTimer(const char *timerName, TickType_t timerPeriod)
{
    TimerHandle_t timer = xTimerCreate(timerName, timerPeriod, pdTRUE, 0, timerCallback);

    if (timer == NULL) {
        printf("Failed to create timer.\n");
        return;
    }

    if (xTimerStart(timer, 0) != pdPASS) {
        printf("Failed to start timer.\n");
        return;
    }
}

void app_pm_enter_pds15(uint32_t timeouts_ms)
{
    if (timeouts_ms) {
        TickType_t timerPeriod = pdMS_TO_TICKS(timeouts_ms);

        char timerName[32];
        snprintf(timerName, sizeof(timerName), "PwrTimer_%u", (unsigned int)timerPeriod);

        createAndStartTimer(timerName, timerPeriod);
    }
}

int cmd_wakeup_timer(int argc, char **argv)
{
    uint32_t timeouts_ms;
    int enable_bcmd = 0;

    if ((argc > 2) && (argv[1] != NULL) && (argv[2] != NULL)) {
        printf("%s\r\n", argv[1]);
        timeouts_ms = atoi(argv[1]);
        enable_bcmd = atoi(argv[2]);
    } else {
        printf("Need timeouts.\r\n");
        return -1;
    }

    enable_multicast_broadcast = enable_bcmd;
    app_pm_enter_pds15(timeouts_ms);
    lp_window_begin();              /* LEAKCAM: main.c, tickless + no 1 s feed wakes in the window */
    pwr_info_clear();

    return 0;
}

int wl_ble_cmd_init(void)
{
    return 0;
}

SHELL_CMD_EXPORT_ALIAS(cmd_wakeup_timer, wakeup_timer, wakeup timer);

static void cmd_lp_pwr(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)pwr_info_get();
}
SHELL_CMD_EXPORT_ALIAS(cmd_lp_pwr, lp_pwr, test120: time split and predicted current of the last sleep window);
