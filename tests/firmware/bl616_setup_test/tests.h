/*
 * The power test firmware's pt_ble.c built into the setup firmware (tests/test120): its test switch and the two
 * helpers it takes from pt_wifi.c, here in main.c.
 */
#ifndef PT_TESTS_H
#define PT_TESTS_H

#define PT_TEST_BLE 1

#include <stdbool.h>
bool pt_radio_init(void);             /* main.c: true once `lp_boot` brought RF up */
int pt_ef_once(void);                 /* main.c: easyflash is up (`lp_boot`) */

#endif
