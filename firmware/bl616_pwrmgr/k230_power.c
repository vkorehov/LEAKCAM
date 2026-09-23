/*
 * K230 power sequencing from the BL616.
 *
 * Hardware chain (POWER.SchDoc): K230_PWR -> U6 0V8 -> PG -> U8 3V3 + U10 1V8 -> PG_1V8 -> U7 1V1
 * -> PG -> RSTN (R30 100k / C23 100n). The chain alone meets the K230 guide's order on a COLD
 * start (0V8 at 90 % ~0.5 ms before 1V8/3V3 begin, RSTN ~12 ms after the last rail).
 * What the hardware does not handle, and this file does:
 *   - 1V8/3V3 have no discharge, so a restart must wait (K230_MIN_OFF_MS) or 0V8 comes up last;
 *   - K230-side pins must not be driven while 3V3 is off (back-feed through the 10 k pull-ups);
 *   - reset is held by the BL616 (Q4) across the whole ramp, independent of the RC.
 */
#include "k230_power.h"
#include "board_pins.h"

#include "bflb_gpio.h"
#include "bflb_uart.h"
#include "bflb_mtimer.h"
#include "log.h"

#define LINK_BAUD             115200
#define RAIL_DISCHARGE_WAIT   5000   /* ms, extra wait for 3V3 to read low after K230_MIN_OFF_MS */
#define RAIL_UP_TIMEOUT_MS    20     /* 3V3 reaches 90 % ~1.7 ms after K230_PWR in the model */
#define RAIL_SETTLE_MS        5      /* after 3V3 seen: 1V1 at 90 % at ~2.9 ms, keep margin */

static struct bflb_device_s *gpio;
static struct bflb_device_s *uart0;
static bool rails_on;
static bool ever_on;
static uint64_t off_at_ms;

static const uint8_t k230_side_pins[] = K230_SIDE_PINS;

void k230_pins_park(void)
{
    /* UART first: once its pads are released it no longer drives TX high */
    bflb_uart_deinit(uart0);
    for (unsigned i = 0; i < sizeof(k230_side_pins); i++) {
        /* analog mode disconnects the input buffer, so a pin sitting between VIL and VIH
         * (the 10 k pull-up to a half-discharged rail) cannot draw shoot-through current */
        bflb_gpio_init(gpio, k230_side_pins[i], GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
    }
}

void k230_link_pins_attach(void)
{
    struct bflb_uart_config_s cfg = {
        .baudrate = LINK_BAUD,
        .direction = UART_DIRECTION_TXRX,
        .data_bits = UART_DATA_BITS_8,
        .stop_bits = UART_STOP_BITS_1,
        .parity = UART_PARITY_NONE,
        .bit_order = UART_LSB_FIRST,
        .flow_ctrl = UART_FLOWCTRL_NONE,
        .tx_fifo_threshold = 7,
        .rx_fifo_threshold = 7,
    };
    bflb_gpio_uart_init(gpio, PIN_LINK_TX, GPIO_UART_FUNC_UART0_TX);
    bflb_gpio_uart_init(gpio, PIN_LINK_RX, GPIO_UART_FUNC_UART0_RX);
    bflb_uart_init(uart0, &cfg);
}

bool k230_alive_level(void)
{
    return bflb_gpio_read(gpio, PIN_K230_ALIVE);
}

bool k230_is_on(void)
{
    return rails_on;
}

static void reset_assert(bool held)
{
    /* inverted by Q4: IO00 high pulls RSTN low */
    if (held)
        bflb_gpio_set(gpio, PIN_K230_RSTN);
    else
        bflb_gpio_reset(gpio, PIN_K230_RSTN);
}

void k230_power_init(void)
{
    gpio = bflb_device_get_by_name("gpio");
    uart0 = bflb_device_get_by_name("uart0");

    /* order matters: reset asserted before K230_PWR is made an output, so that no state of
     * this sequence can release RSTN with the rails half up */
    bflb_gpio_init(gpio, PIN_K230_RSTN, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    reset_assert(true);
    bflb_gpio_init(gpio, PIN_K230_PWR, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    bflb_gpio_reset(gpio, PIN_K230_PWR);
    /* IO01: no internal pull; R32 pulls it to the switched 3V3 rail */
    bflb_gpio_init(gpio, PIN_K230_ALIVE, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
    k230_pins_park();
    rails_on = false;
}

static bool wait_level(bool level, uint32_t timeout_ms)
{
    uint64_t t0 = bflb_mtimer_get_time_ms();
    while (bflb_mtimer_get_time_ms() - t0 < timeout_ms) {
        if (k230_alive_level() == level)
            return true;
        bflb_mtimer_delay_ms(1);
    }
    return false;
}

enum k230_result k230_power_on(void)
{
    if (rails_on)
        return K230_OK;

    reset_assert(true);
    k230_pins_park();

    /* 1. rails must be discharged, or 0V8 will come up after 1V8/3V3 */
    if (ever_on) {
        uint64_t off_for = bflb_mtimer_get_time_ms() - off_at_ms;
        if (off_for < K230_MIN_OFF_MS) {
            LOG_I("k230: waiting %u ms for 1V8/3V3 to discharge\r\n", (unsigned)(K230_MIN_OFF_MS - off_for));
            bflb_mtimer_delay_ms(K230_MIN_OFF_MS - off_for);
        }
    }
    /* IO01 sees the 3V3 rail through R32 while the K230 is unpowered. It only tells us the rail
     * is below VIL (~1 V), not below 0.33 V; the timer above is what actually protects 1V8. */
    if (!wait_level(false, RAIL_DISCHARGE_WAIT)) {
        LOG_E("k230: 3V3 still above ~1 V after discharge wait, something back-feeds the rail\r\n");
        return K230_ERR_RAIL_NOT_DISCHARGED;
    }

    /* 2. enable the chain; the PG links do the rest in ~3 ms */
    bflb_gpio_set(gpio, PIN_K230_PWR);
    rails_on = true;
    ever_on = true;

    /* 3. 3V3 must appear. K230 GPIO2 is JTAG_TCK at reset; if its default pull fights R32 the
     * level may stay marginal, so this is a hard error only when nothing is seen at all. */
    if (!wait_level(true, RAIL_UP_TIMEOUT_MS)) {
        LOG_E("k230: 3V3 not seen on IO01 within %u ms, powering off\r\n", RAIL_UP_TIMEOUT_MS);
        k230_power_off();
        return K230_ERR_RAIL_NOT_UP;
    }
    bflb_mtimer_delay_ms(RAIL_SETTLE_MS);

    /* 4. only now may the link pins drive: 3V3 and the pull-ups are powered */
    k230_link_pins_attach();

    /* 5. release reset; R30/C23 add another ~12 ms before RSTN crosses 1.26 V */
    reset_assert(false);
    LOG_I("k230: rails up, reset released\r\n");
    return K230_OK;
}

void k230_power_off(void)
{
    /* park before the rail drops: from here until the next power-up nothing drives into 3V3 */
    k230_pins_park();
    reset_assert(true);
    bflb_mtimer_delay_ms(1);                  /* RSTN low (Q4) before the core collapses */
    bflb_gpio_reset(gpio, PIN_K230_PWR);     /* all ENs fall together: R27/R28 pull up to K230_PWR */
    if (rails_on)
        off_at_ms = bflb_mtimer_get_time_ms();
    rails_on = false;
    LOG_I("k230: rails off\r\n");
}

void k230_hard_reset(void)
{
    reset_assert(true);
    bflb_mtimer_delay_ms(10);
    reset_assert(false);
}
