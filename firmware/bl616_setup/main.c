/*
 * LEAKCAM BL616 setup firmware (DESIGN.md 5.3.1): runs while there is no working presence. It sleeps in PDS and
 * advertises BLE every 2 s, so the app can always reach the board; a phone sets the Wi-Fi as in the full firmware
 * (ble_pairing.c, wifi_check.c). USB-C 1 plugged in switches to the full firmware. Only what setup mode needs.
 *
 * All setup runs here, the normal run in the full firmware (user, 2026-11-02). The switch to the full firmware
 * (switch_to_full.c, slot.c): on a USB plug once the stored network passes its check up to the API, and after new credentials
 * (checked the same way); back by itself after a phone's wake (2 min) or every 6 h while a network is stored
 * (tests/test125). Not built yet: the USB cable setup (tests/test113).
 */
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "bl616_hbn.h"

#include "ble_pairing.h"
#include "crash_rec.h"
#include "switch_to_full.h"
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
    switch_to_full_after_credentials();
}

void setup_usb_plugged(void)
{
    ble_pairing_adv_interval(SETUP_ADV_FAST_MS);   /* awake from now on: the phone connects sooner */
    switch_to_full_on_usb();
}

static void boot_task(void *arg)
{
    (void)arg;
    setup_boot();
    bool from_full = switch_to_full_start();     /* the way back to the full firmware */
    if (setup_usb_present() && !from_full)  /* plugged in: from the full firmware it is not a plug */
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
