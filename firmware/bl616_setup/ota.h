#pragma once

#include <stdbool.h>

/* The full firmware's upgrade, unpacked here (DESIGN.md 5.3.7, tests/test130): the .xz copy the full firmware
 * downloaded and checked into the end of slot 0 (ota_env.h, bl616_full/ota_fetch.c) checked again, unpacked over the
 * old image, the result's SHA-256 checked, then the switch to it. No network: a power cut restarts the unpack from the
 * intact copy. At the start, after switch_to_full_start(); true: an upgrade is pending (the firmware stays awake for
 * it, no sleep). */
bool ota_start(void);

/* true while an upgrade is pending or slot 0 is half written: no switch to the full firmware */
bool ota_blocks_full(void);
