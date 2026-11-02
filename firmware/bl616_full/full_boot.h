/*
 * The full firmware's start: storage and RF once, then presence (presence.c). No BLE (DESIGN.md 5.3, user 2026-11-02):
 * no network stored -> the setup firmware at once (switch_to_setup.c), where a phone sets it.
 */
#ifndef LEAKCAM_FULL_BOOT_H
#define LEAKCAM_FULL_BOOT_H

void full_boot_start(void);           /* the start task: storage, RF, presence */

#endif
