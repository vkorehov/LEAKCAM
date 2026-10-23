/*
 * The BL616 never stays hung: a reset-mode watchdog (WDG_GUARD_MS) fed once a second by a task at the top priority.
 * A crash, an exception loop, interrupts left off or a stalled scheduler stop the feeding and the chip resets and
 * comes back by itself (2026-10-23: the board dropped off USB twice and only EN brought it back; there was no
 * watchdog). Shell commands that busy-wait run below the feeder, so they cannot starve it. HBN powers it down.
 */
#ifndef LEAKCAM_WDG_GUARD_H
#define LEAKCAM_WDG_GUARD_H

#include <stdbool.h>

#define WDG_GUARD_MS 8000u

bool wdg_guard_start(void);           /* right after board_init(), before the scheduler; true: last reset was ours */

#endif
