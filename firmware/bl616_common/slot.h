#pragma once

/* The two BL616 firmwares (DESIGN.md 5.3.1) are the two images of boot2's FW partition: slot 0 at 0x10000 (full),
 * slot 1 at 0x210000 (setup). */

/* the slot this firmware runs from (0 or 1); -1 when the partition table cannot be read */
int slot_active(void);

/* marks the other slot active and resets into it; returns only when the partition table write failed */
int slot_switch(void);
