/*
 * LEAKCAM BL616 crash recorder: every exception, watchdog hang, assert, stack overflow and malloc failure leaves a
 * record with a stack backtrace in HBN RAM (.hbn_noinit), which survives the reset; the next boot prints it
 * (crash_rec_report) and `crash` shows or clears it. Decode on the host against the same ELF:
 *   tools/power-bringup/crash_decode.py <elf> <console text>
 * User 2026-10-31: hard-to-reproduce issues need backtraces; every firmware keeps this.
 */
#ifndef LEAKCAM_CRASH_REC_H
#define LEAKCAM_CRASH_REC_H

#include <stdint.h>

void crash_rec_report(void);          /* after the console is up: prints the record of the last crash, if any */
void crash_rec_fed(void);             /* the watchdog feeder after each feed (wdg_guard.c): the hang detector's clock */

#endif
