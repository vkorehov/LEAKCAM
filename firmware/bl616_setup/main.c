/*
 * LEAKCAM BL616 setup firmware (DESIGN.md 5.3.1): runs while there is no working presence. It sleeps in PDS and
 * advertises BLE every 2 s, so the app can always reach the board; a phone sets the Wi-Fi as in the full firmware
 * (ble_pairing.c, wifi_check.c). USB-C 1 plugged in switches to the full firmware. Only what setup mode needs.
 *
 * All setup runs here, the normal run in the full firmware (user, 2026-11-02). The switch to the full firmware
 * (switch_to_full.c, slot.c): on a USB plug once the stored network passes its check up to the API, and after new credentials
 * (checked the same way); back by itself after a phone's wake (2 min) or every 6 h while a network is stored
 * (tests/test125). Setup over the USB cable (usb_setup.c, tests/test113): with USB power and setup staying, the
 * K230 is powered with WAKE setup and relays the phone's lines.
 */
#include "blecore.h"
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "bl616_glb.h"
#include "bl616_hbn.h"

#include "ble_pairing.h"
#include "crash_rec.h"
#include "k230_power.h"
#include "k230_session.h"
#include "metrics.h"
#include "usb_setup.h"
#include "switch_to_full.h"
#include "ota.h"
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
    k230_power_init();              /* the K230 off and its pins parked before anything else uses them */
    k230_session_init();
    usb_setup_init();
    setup_boot();
    bool from_full = switch_to_full_start();     /* the way back to the full firmware */
    bool ota = ota_start();                     /* the full firmware's upgrade: written now, awake until done */
    if (setup_usb_present() && !from_full) {    /* plugged in: from the full firmware it is not a plug */
        setup_usb_plugged();
    } else if (!ota) {
        setup_sleep_begin(false);   /* BLE-idle veto until advertising runs (setup_sleep.c) */
        if (setup_usb_present())
            usb_setup_begin();      /* sent here by the full firmware on USB power: the cable setup is offered */
    }
    vTaskDelete(NULL);
}

int main(void)
{
    /* HBN RAM rule (tests/test021): retention/sleep mode off before the first HBN RAM access */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    metrics_boot();                 /* boots and the reset cause before the PDS reset below (metrics.h) */
    /* the PDS block reset the BLE controller's own init would do (ble_pairing.c keeps that hook empty: done there it
     * took the running USB device with it): here, before board_init() brings USB up, it clears a BLE core left asleep
     * by the run before (a reset during the battery sleep), which no other reset but EN clears (bugs/bug-006) */
    blecore_dump(BLECORE_BOOT);     /* the PDS registers the run before left (a wedge's 3459fbb0/03303003/00000300) */
    GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_PDS);
    board_init();                   /* also the USB console and its shell (defconfig) */
    crash_rec_report();
    (void)wdg_guard_start();
    if (xTaskCreate(boot_task, "boot", 1024, NULL, 10, NULL) != pdPASS)
        vAssertCalled();
    vTaskStartScheduler();
    while (1) {
    }
}
