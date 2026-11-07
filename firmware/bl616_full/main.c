/*
 * LEAKCAM BL616 product firmware.
 *
 * No BLE (DESIGN.md 5.3, user 2026-11-02): each time BLE is needed it switches to the setup firmware (slot.c): no
 * network stored, a check-in that did not reach the server, or the person's delete in the app (no user left). The
 * setup firmware sets the Wi-Fi and comes back (firmware/bl616_setup/switch_to_full.c).
 *
 * Use case 2, presence (tests/test111): the BL616 boots with the K230 held off and checks in at the server every
 * poll interval (presence.c). Built only from code the tests proved (skill leakcam-product-firmware); the next use
 * cases add HBN sleep, the K230 session and the wake sources.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "bl616_hbn.h"

#include "crash_rec.h"
#include "k230_power.h"
#include "full_boot.h"
#include "metrics.h"
#include "usb_power.h"
#include "wdg_guard.h"

int main(void)
{
    /* HBN RAM rule (tests/test021): with HBN_SRAM retention or sleep mode set, an HBN RAM access right after an AON
     * register write is corrupted; the mode survives resets. Both off before the first HBN RAM access (the crash
     * record lives there). */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    metrics_boot();                     /* boots and the reset cause, for the check-in (metrics.h) */
    board_init();                       /* clocks, the USB CDC console and its shell */
    crash_rec_report();                 /* the last crash's record, if any, at every boot */
    (void)wdg_guard_start();            /* hangs are reset and recorded (crash_rec.c, `crash`): the record tells a
                                         * watchdog reset, the flag is not needed here */
    k230_power_init();                  /* K230 held off: reset asserted, rails off, its pins parked */
    usb_sense_init();
    full_boot_start();
    vTaskStartScheduler();
    for (;;) {
    }
}
