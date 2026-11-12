#pragma once

#include <stdbool.h>

/* The setup firmware switching to the full firmware (DESIGN.md 5.3.1; slot.c, tests/test126):
 *   - USB-C 1 plugged in: the stored network is checked up to the LEAKCAM API first (join, address, internet, a
 *     check-in answered: api.h); only a pass switches. No network stored, or a failed check: setup stays (awake on
 *     USB power, advertising), the failed step on the console.
 *   - new credentials stored over BLE (already checked the same way, ble_pairing.c): switch once the phone has read
 *     SAVED and left (at most SWITCH_TO_FULL_PHONE_MS).
 *   - back by itself (switch_to_full_start): after a phone's wake through the server ("wake" from the full firmware) in
 *     SWITCH_TO_FULL_WAKE_S, else every SWITCH_TO_FULL_RETRY_S while a network is stored (the full firmware tries its check-in and
 *     comes back here if it fails, tests/test125); never while a phone is connected or a switch runs.
 * Both run in a task of their own (a check takes up to a minute); a second request while one runs is dropped. */
void switch_to_full_on_usb(void);
void switch_to_full_after_credentials(void);
/* a humidity alarm (climate.c, DESIGN.md 5.3.15): to the full firmware when a network is stored (it alarms at its first
 * reading), after a phone left; none stored: setup stays */
void switch_to_full_humid(void);

/* at the start, after easyflash: reads (and deletes) why the full firmware switched here ("setup_why": "lost", "wake";
 * none after a power-on or a flash) and starts the way back; true when the full firmware sent it here (a USB power
 * present at the start is then not a plug: no switch check) */
bool switch_to_full_start(void);
