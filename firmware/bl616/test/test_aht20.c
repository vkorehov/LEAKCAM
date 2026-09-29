/* Host test: AHT20 frame parsing, conversion and CRC against the datasheet (v2024-07, 5.2). */
#include <stdio.h>
#include "aht20_calc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    /* CRC-8 poly 0x31 init 0xFF: standard check value for bytes BE EF is 0x92 */
    const uint8_t v[2] = { 0xBE, 0xEF };
    CHECK(aht20_crc8(v, 2) == 0x92, "crc8(BE EF) = %02X, want 92", aht20_crc8(v, 2));

    /* frame: status 0x1C (idle, calibrated), SRH 0x80000 (50 %), ST 0x2FFAB (datasheet example) */
    uint8_t f[7] = { 0x1C, 0x80, 0x00, 0x02, 0xFF, 0xAB, 0 };
    f[6] = aht20_crc8(f, 6);
    CHECK(aht20_raw_rh(f) == 0x80000, "SRH %05X", (unsigned)aht20_raw_rh(f));
    CHECK(aht20_raw_t(f) == 0x2FFAB, "ST %05X", (unsigned)aht20_raw_t(f));
    CHECK(aht20_rh_x10(aht20_raw_rh(f)) == 500, "RH x10 = %d, want 500", aht20_rh_x10(aht20_raw_rh(f)));
    /* datasheet: ST 0x2FFAB = 196523 -> T = 196523 / 1048576 * 200 - 50 = -12.5 C */
    CHECK(aht20_t_x10(aht20_raw_t(f)) == -125, "T x10 = %d, want -125", aht20_t_x10(aht20_raw_t(f)));
    CHECK(aht20_rh_x10(0xFFFFF) == 1000, "RH full scale %d", aht20_rh_x10(0xFFFFF));
    CHECK(aht20_t_x10(0) == -500, "T zero %d", aht20_t_x10(0));
    f[4] ^= 0x01;
    CHECK(aht20_crc8(f, 6) != f[6], "CRC misses a flipped bit");

    printf(fails ? "aht20: %d failures\n" : "aht20: CRC, frame layout and conversions match the datasheet\n", fails);
    return fails != 0;
}
