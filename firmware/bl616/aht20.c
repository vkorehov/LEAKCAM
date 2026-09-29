#include "aht20.h"
#include "aht20_calc.h"
#include "board_pins.h"

#include "bflb_gpio.h"
#include "bflb_i2c.h"
#include "bflb_mtimer.h"
#include "log.h"

#define AHT20_ADDR      0x38      /* datasheet writes it as 0x70/0x71 (8-bit with R/W) */
#define AHT20_I2C_HZ    100000    /* datasheet: 10-400 kHz */
/* datasheet 5.2 says wait 80 ms; its note says a read before the end shows status bit 7 (busy)
 * set, so polling from earlier is safe and takes the sample as soon as it is there */
#define AHT20_FIRST_READ_MS 50
#define AHT20_POLL_MS   5
#define AHT20_GIVE_UP_MS 100

static struct bflb_device_s *i2c0;
static uint64_t started_ms;                 /* when the running measurement was triggered */

static int xfer(uint16_t flags, uint8_t *buf, uint16_t len)
{
    struct bflb_i2c_msg_s msg = { .addr = AHT20_ADDR, .flags = flags, .buffer = buf, .length = len };
    return bflb_i2c_transfer(i2c0, &msg, 1);
}

void aht20_init(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    /* external 4.7 k pull-ups to 3V3_SLEEP, so no internal pulls */
    bflb_gpio_init(gpio, PIN_TEMP_SCL, GPIO_FUNC_I2C0 | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
    bflb_gpio_init(gpio, PIN_TEMP_SDA, GPIO_FUNC_I2C0 | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
    i2c0 = bflb_device_get_by_name("i2c0");
    bflb_i2c_init(i2c0, AHT20_I2C_HZ);
}

void aht20_deinit(void)
{
    if (!i2c0)
        return;
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    bflb_i2c_deinit(i2c0);
    i2c0 = NULL;
    bflb_gpio_init(gpio, PIN_TEMP_SCL, GPIO_INPUT | GPIO_FLOAT);
    bflb_gpio_init(gpio, PIN_TEMP_SDA, GPIO_INPUT | GPIO_FLOAT);
}

enum aht20_result aht20_start(void)
{
    uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    started_ms = bflb_mtimer_get_time_ms();
    return xfer(I2C_M_WRITE, trig, sizeof(trig)) < 0 ? AHT20_ERR_I2C : AHT20_OK;
}

/* the frame of the measurement aht20_start() triggered: first read 50 ms after it, then every
 * 5 ms while the sensor is busy */
static enum aht20_result wait_frame(uint8_t f[7])
{
    uint64_t done = started_ms + AHT20_FIRST_READ_MS, now = bflb_mtimer_get_time_ms();
    if (now < done)
        bflb_mtimer_delay_ms((uint32_t)(done - now));
    while (1) {
        if (xfer(I2C_M_READ, f, 7) < 0)
            return AHT20_ERR_I2C;
        if (!(f[0] & AHT20_STATUS_BUSY))
            return AHT20_OK;
        if (bflb_mtimer_get_time_ms() - started_ms >= AHT20_GIVE_UP_MS)
            return AHT20_ERR_BUSY;              /* busy: the data would be the previous sample's */
        bflb_mtimer_delay_ms(AHT20_POLL_MS);
    }
}

enum aht20_result aht20_finish(int *rh_x10, int *t_x10)
{
    uint8_t f[7];
    for (int attempt = 0; attempt < 2; attempt++) {
        enum aht20_result r = wait_frame(f);
        if (r != AHT20_OK)
            return r;
        if (!(f[0] & AHT20_STATUS_CAL)) {
            /* calibration not enabled (older parts after a brown-out): load it and measure again */
            uint8_t init[3] = { 0xBE, 0x08, 0x00 };
            xfer(I2C_M_WRITE, init, sizeof(init));
            bflb_mtimer_delay_ms(10);
            if ((r = aht20_start()) != AHT20_OK)
                return r;
            continue;
        }
        if (aht20_crc8(f, 6) != f[6])
            return AHT20_ERR_CRC;
        *rh_x10 = aht20_rh_x10(aht20_raw_rh(f));
        *t_x10 = aht20_t_x10(aht20_raw_t(f));
        return AHT20_OK;
    }
    return AHT20_ERR_BUSY;
}
