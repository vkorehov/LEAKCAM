/*
 * K230 power sequencing from the BL616.
 *
 * Hardware chain (POWER.SchDoc): K230_PWR -> U6 0V8 -> PG -> U8 3V3 + U10 1V8 -> PG_1V8 -> U7 1V1
 * -> PG -> RSTN (R30 100k / C23 100n). The chain alone meets the K230 guide's order on a COLD
 * start (0V8 at 90 % ~0.5 ms before 1V8/3V3 begin, RSTN ~12 ms after the last rail).
 * What the hardware does not handle, and this file does:
 *   - 1V8/3V3 have no active discharge, only RD1/RD2 (1.5 k): k230_power_off() waits
 *     OFF_DISCHARGE_MS, so the next power-up cannot bring 0V8 up after them;
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
/* 1V8/3V3 below 10 % after 56-82 ms through RD1/RD2 whatever else loads them (firmware/sim);
 * 300 ms is 3.6 x that, for capacitor tolerance */
#define OFF_DISCHARGE_MS      300
#define RAIL_UP_MS            5      /* all rails at 90 % 2.9 ms after K230_PWR (firmware/sim) */

static struct bflb_device_s *gpio;
static struct bflb_device_s *uart0;
static bool rails_on;

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

void k230_reset_release(void)
{
    /* R30/C23 add another ~12 ms before RSTN crosses 1.26 V */
    reset_assert(false);
    LOG_I("k230: reset released\r\n");
}

void k230_power_on(bool hold_reset)
{
    if (rails_on)
        return;

    reset_assert(true);
    k230_pins_park();

    /* 1. enable the chain; the PG links sequence the rails, all at 90 % in ~3 ms. They start
     * discharged: at boot they were off, and every k230_power_off() waits for them. Nothing is
     * read back here: IO01 (K230_ALIVE) is the heartbeat only, and in reset its K230 pad is
     * JTAG_TCK with a pull-down against R32, so its level says nothing about 3V3. A rail that
     * never comes up shows as no READY. */
    bflb_gpio_set(gpio, PIN_K230_PWR);
    rails_on = true;
    bflb_mtimer_delay_ms(RAIL_UP_MS);

    /* 2. only now may the link pins drive: 3V3 and the pull-ups are powered */
    k230_link_pins_attach();

    /* 3. release reset, unless the caller wants the K230 held (first boot: its console, the
     * CH340X on the same 3V3, needs time to enumerate before the K230 prints anything) */
    LOG_I("k230: rails up\r\n");
    if (!hold_reset)
        k230_reset_release();
}

void k230_power_off(void)
{
    /* park before the rail drops: from here until the next power-up nothing drives into 3V3 */
    k230_pins_park();
    reset_assert(true);
    bflb_mtimer_delay_ms(1);                  /* RSTN low (Q4) before the core collapses */
    bflb_gpio_reset(gpio, PIN_K230_PWR);     /* all ENs fall together: R27/R28 pull up to K230_PWR */
    if (rails_on)
        bflb_mtimer_delay_ms(OFF_DISCHARGE_MS);   /* 1V8/3V3 down through RD1/RD2 */
    rails_on = false;
    LOG_I("k230: rails off\r\n");
}
