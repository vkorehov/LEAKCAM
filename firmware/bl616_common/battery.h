#pragma once

#include <stdbool.h>

/* The battery as the K230 measured it (DESIGN.md 5.3.9, tests/test046): every K230 frame carries ;b=<mV>
 * (k230_link.h). Kept in HBN RAM (survives sleep, resets and the switch between the two firmwares; lost only with the
 * battery) for every channel to the phone: the check-in (bat=, chg=), the BLE advertisement, the USB setup's ID. The
 * K230 reads the charger's output rail on USB power, not the cell: such a reading is not kept, the last one on battery
 * stays. */

/* a K230 reading (-1: its ADC failed, ignored) */
void battery_seen(int mv);

/* the last reading on battery, -1 = none since the battery was connected */
int battery_mv(void);

/* on USB power now (PGOOD): the charger charges the cell (until full). Each firmware's own PGOOD reader (the full
 * firmware's is the analog comparator: this module never touches the pin) */
bool battery_usb_power(void);
