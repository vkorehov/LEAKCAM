/*
 * LEAKCAM BL616 setup firmware (DESIGN.md 5.3.1): runs while there is no working presence. It sleeps in PDS and
 * advertises BLE every 2 s, so the app can always reach the board; a phone sets the Wi-Fi as in the full firmware
 * (ble_pairing.c, wifi_check.c). USB-C 1 plugged in hands over to the full firmware. Only what setup mode needs.
 *
 * Not built yet (tests to come): the slot switch to the full firmware: on a USB plug, after new credentials, and every
 * 6 h for the full firmware's check-in (presence is proved there, not here: DESIGN.md 5.3.1).
 */
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "bl616_hbn.h"

#include "ble_pairing.h"
#include "crash_rec.h"
#include "setup_boot.h"
#include "setup_sleep.h"
#include "wdg_guard.h"

/* static memory for the idle task (configSUPPORT_STATIC_ALLOCATION) */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[1024];

    *tcb = &idle_tcb;
    *stack = idle_stack;
    *size = 1024;
}

void ble_credentials_stored(void)
{
    printf("setup: credentials stored (the switch to the full firmware: not built yet)\r\n");
}

void setup_usb_plugged(void)
{
    printf("setup: USB plugged (the switch to the full firmware: not built yet)\r\n");
}

static void boot_task(void *arg)
{
    (void)arg;
    setup_boot();
    if (setup_usb_present())
        setup_usb_plugged();
    else
        setup_sleep_begin(false);   /* BLE-idle veto until advertising runs (setup_sleep.c) */
    vTaskDelete(NULL);
}

int main(void)
{
    /* HBN RAM rule (tests/test021): retention/sleep mode off before the first HBN RAM access */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    board_init();                   /* also the USB console and its shell (defconfig) */
    crash_rec_report();
    (void)wdg_guard_start();
    if (xTaskCreate(boot_task, "boot", 1024, NULL, 10, NULL) != pdPASS)
        vAssertCalled();
    vTaskStartScheduler();
    while (1) {
    }
}
