/*
 * AHT20 (U17) humidity/temperature sensor on the BL616's I2C0: SCL IO28, SDA IO29, R73/R74 4.7 k
 * to 3V3_SLEEP, VDD on 3V3_SLEEP, 7-bit address 0x38.
 *
 * The AHT20 has no interrupt or alert pin, so humidity cannot wake the BL616; it is sampled on
 * RTC wake-ups from hibernate. It needs no power-up delay beyond the first 5 ms of 3V3_SLEEP,
 * which the BL616 boot already exceeds, and sleeps at <= 0.2 uA between reads.
 */
#ifndef LEAKCAM_AHT20_H
#define LEAKCAM_AHT20_H

#include <stdint.h>

enum aht20_result {
    AHT20_OK = 0,
    AHT20_ERR_I2C,        /* no ACK: sensor missing, bus stuck, pull-ups unpowered */
    AHT20_ERR_BUSY,       /* still measuring after the 80 ms wait */
    AHT20_ERR_CRC,
};

void aht20_init(void);
/* one measurement, ~85 ms blocking; tenths of %RH and degC */
enum aht20_result aht20_read(int *rh_x10, int *t_x10);
/* release the pins before hibernate: the pull-ups keep the bus idle-high, no current flows */
void aht20_deinit(void);

#endif
