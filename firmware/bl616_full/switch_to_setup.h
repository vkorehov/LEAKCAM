/*
 * The full firmware switching to the setup firmware (DESIGN.md 5.3.1; slot.c, tests/test126): every BLE need goes
 * there, the full firmware has no BLE (user, 2026-11-02). Triggers: no network stored (full_boot.c, presence.c), a
 * check-in that did not reach the server ("lost", tests/test117), "ble":1 in the check-in answer ("wake", a phone out
 * of range asked through the server, tests/test119).
 */
#ifndef LEAKCAM_SWITCH_TO_SETUP_H
#define LEAKCAM_SWITCH_TO_SETUP_H

/* why ("lost", "wake") goes to easyflash "setup_why" for the setup firmware's way back (switch_to_full.c there); NULL
 * when no network is stored (it stays until a phone or the cable sets one); returns only when the switch failed */
void switch_to_setup(const char *why);

#endif
