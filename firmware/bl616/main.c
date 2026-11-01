/*
 * LEAKCAM BL616 product firmware.
 *
 * Version 1: use case 1, Wi-Fi provisioning from the phone (tests/test040, tests/test106). The BL616 boots with the
 * K230 held off; while USB powers the board it advertises the provisioning service, checks the network the phone
 * sends (join, address, internet) and stores it. Built only from code those tests proved (skill
 * leakcam-product-firmware); the next use cases add HBN sleep, the K230 session and the wake sources.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "bl616_hbn.h"

#include "crash_rec.h"
#include "k230_power.h"
#include "provision.h"
#include "usb_power.h"
#include "wdg_guard.h"

int main(void)
{
    /* HBN RAM rule (tests/test021): with HBN_SRAM retention or sleep mode set, an HBN RAM access right after an AON
     * register write is corrupted; the mode survives resets. Both off before the first HBN RAM access (the crash
     * record lives there). */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    board_init();                       /* clocks, the USB CDC console and its shell */
    crash_rec_report();                 /* the last crash's record, if any, at every boot */
    (void)wdg_guard_start();            /* hangs are reset and recorded (crash_rec.c, `crash`): the record tells a
                                         * watchdog reset, the flag is not needed here */
    k230_power_init();                  /* K230 held off: reset asserted, rails off, its pins parked */
    usb_sense_init();
    provision_start();
    vTaskStartScheduler();
    for (;;) {
    }
}
