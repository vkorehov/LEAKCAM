/*
 * AHT20 frame arithmetic (datasheet v2024-07, section 5.2), header-only so it can be unit-tested
 * on a host without the SDK.
 *
 * Read frame (7 bytes after the 0xAC 0x33 0x00 trigger and 80 ms):
 *   [0] status  [1..2.5] SRH[19:0]  [3.5..5] ST[19:0]  [6] CRC8 over bytes 0..5
 *   RH = SRH / 2^20 * 100 %         T = ST / 2^20 * 200 - 50 degC
 * CRC8: polynomial x^8 + x^5 + x^4 + 1 (0x31), initial value 0xFF.
 */
#ifndef LEAKCAM_AHT20_CALC_H
#define LEAKCAM_AHT20_CALC_H

#include <stdint.h>

#define AHT20_STATUS_BUSY 0x80
#define AHT20_STATUS_CAL  0x08

static inline uint8_t aht20_crc8(const uint8_t *p, unsigned n)
{
    uint8_t crc = 0xFF;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

static inline uint32_t aht20_raw_rh(const uint8_t f[7])
{
    return ((uint32_t)f[1] << 12) | ((uint32_t)f[2] << 4) | (f[3] >> 4);
}

static inline uint32_t aht20_raw_t(const uint8_t f[7])
{
    return ((uint32_t)(f[3] & 0x0F) << 16) | ((uint32_t)f[4] << 8) | f[5];
}

/* fixed point, tenths: 1000 = 100.0 %RH, 215 = 21.5 degC (rounded to nearest) */
static inline int aht20_rh_x10(uint32_t srh)
{
    return (int)(((uint64_t)srh * 1000u + (1u << 19)) >> 20);
}

static inline int aht20_t_x10(uint32_t st)
{
    return (int)((((uint64_t)st * 2000u + (1u << 19)) >> 20)) - 500;
}

#endif
