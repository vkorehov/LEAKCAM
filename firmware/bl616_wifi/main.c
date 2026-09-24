/*
 * LEAKCAM BL616 Wi-Fi bridge, standalone bring-up firmware.
 *
 * Powers the K230 up the same way the power manager does (bl616_pwrmgr/k230_power.c) and starts
 * the Wi-Fi link, then leaves both running: enough to bring up the K230's SDIO Wi-Fi driver
 * on the bench. In the product this is not a firmware of its own: wifi_link_start() and
 * wifi_link_stop() are called from the power manager's session around k230_power_on() and
 * k230_power_off() (see README.md).
 */
#include <FreeRTOS.h>
#include "task.h"

#include "board.h"
#include "log.h"

#include "k230_power.h"
#include "wifi_link.h"

static void session_task(void *arg)
{
    (void)arg;
    if (k230_power_on() != K230_OK) {
        LOG_E("k230 power-up failed, Wi-Fi not started\r\n");
    } else if (wifi_link_start() != 0) {
        LOG_E("Wi-Fi link start failed\r\n");
    }
    vTaskDelete(NULL);
}

int main(void)
{
    board_init();
    /* rails off, reset held, K230-side pins parked, before anything else can drive them */
    k230_power_init();

    xTaskCreate(session_task, "session", 1024, NULL, 10, NULL);
    vTaskStartScheduler();
    while (1) {
    }
}
