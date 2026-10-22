/*
 * LEAKCAM BL616 bring-up firmware: board tests on the PR1 USB console (not the product firmware).
 * Which tests are built in: tests.h. Rails start OFF at boot; nothing switches until you type.
 *
 *   pt_status          everything at a glance
 *   pt_mon [on|off]    one status line per second
 *
 * POWER  pt_on / pt_off    K230 rails on (K230 held in reset) / off, with the 3V3 ramp and
 *                          discharge timed; pt_run releases the reset (a blank NAND then makes the
 *                          K230 ROM enumerate on USB-C 1 as 29f1:0230), pt_hold holds it again,
 *                          pt_cycle [n] power-cycles n times.
 *        pt_alive [s]      K230_ALIVE heartbeat (IO01) for s seconds (default 10): edges, half-period
 *                          min/avg/max, and the first edge's time after pt_run.
 *        What the BL616 sees of the rails: the switched 3V3 through the 10 k pull-ups on the SDIO
 *        lines, read as inputs only. 0V8 is implied (U8 is enabled by U6's power-good); 1V8 and
 *        1V1 are not visible: measure them (power_check.py) or use pt_run.
 * UART   pt_uart on|off|send <text>   UART0 = the K230 link (IO21 TX, IO22 RX, LINK_BAUD 8N1): one
 *                          message per second, anything received is echoed here. Only while 3V3 is
 *                          up (the link pins are pulled up to it); pt_off stops it first.
 * I2C    pt_i2c [scan]     AHT20 reading (0x38), or a scan of the whole bus (IO28/IO29, 3V3_SLEEP)
 * USB    pt_usb            USB cable on USB-C 1 (BQ24072 PGOOD); plugging and unplugging is reported
 * LEAK   pt_leak           probe node (IO20, ADC) and the probe resistance across S1..S3
 * BOOT1  pt_boot1 low|high|park   IO27 -> RBT1 10k -> K230 BOOT1. Low at start: with IO27 parked, BOOT1 read
 *                          0.5 V on rev 1 (2026-10-17), at the 1.8 V bank's VIL, and the ROM did not boot the
 *                          NAND. High = the ROM's SD-card path, which fails into USB boot (re-flashing).
 */
#include "../../../firmware/bl616/k230_link.h"   /* LINK_BAUD: the link's one rate */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include "shell.h"
#include "tests.h"

#include "../../../firmware/bl616/board_pins.h"   /* one pin map for the product and this test */

#if PT_TEST_UART
#include "bflb_uart.h"
#endif
#if PT_TEST_I2C
#include "../../../firmware/bl616/aht20.h"
#include "bflb_i2c.h"
#endif
#if PT_TEST_LEAK
#include "bflb_adc.h"
#include "bflb_acomp.h"
#endif
#if PT_TEST_SLEEP
#include "bl616_hbn.h"
#include "bl616_pm.h"
#endif
#if PT_TEST_SDU
#include "bflb_sdio2.h"
#include "bflb_l1c.h"
#include "bl616_glb.h"
static void sdu_stop(void);
#endif


static struct bflb_device_s *gpio;
static SemaphoreHandle_t hw;                       /* commands and the monitor share the peripherals */
static bool rails_on, reset_held = true, monitor;
static volatile bool console_open;

static void park(uint8_t pin)
{
    /* analog: no output, no input buffer (a half-powered pull-up cannot cause shoot-through) */
    bflb_gpio_init(gpio, pin, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
}

enum boot1_mode { BOOT1_LOW, BOOT1_HIGH, BOOT1_PARK };
static enum boot1_mode boot1 = BOOT1_PARK;
static const char *const boot1_name[] = { "low (NAND boot)", "HIGH (ROM tries SD, then USB boot)", "parked (floating)" };

/* ---------------------------------------------------------------- K230 power ---- */
static const uint8_t sense_pins[] = { PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3, PIN_SD_CMD };
#define SENSE_ALL ((unsigned)sizeof(sense_pins))
#define SENSE_UP  (SENSE_ALL - 1)                  /* one line may be held by the K230 pad itself */

static uint64_t run_us;                            /* when pt_run released the reset */

static void reset_hold(bool held)
{
    /* inverted by Q4: IO00 high pulls RSTN low */
    if (held)
        bflb_gpio_set(gpio, PIN_K230_RSTN);
    else
        bflb_gpio_reset(gpio, PIN_K230_RSTN);
    reset_held = held;
}

/* SDIO lines (10 k pull-ups to the switched 3V3) that read high; they are plain inputs */
static unsigned sense_high(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < SENSE_ALL; i++)
        n += bflb_gpio_read(gpio, sense_pins[i]) ? 1 : 0;
    return n;
}

#if PT_TEST_UART
static struct bflb_device_s *uart0;
static bool uart_on;
static volatile bool uart_echo;      /* test018: every received byte sent straight back */
static unsigned uart_count;

static void uart_stop(void)
{
    uart_echo = false;
    if (!uart_on)
        return;
    bflb_uart_deinit(uart0);               /* first, so TX no longer drives the pad high */
    park(PIN_LINK_TX);
    park(PIN_LINK_RX);
    uart_on = false;
}

static bool uart_start(void)
{
    if (!rails_on || sense_high() < SENSE_UP)
        return false;
    struct bflb_uart_config_s cfg = {
        .baudrate = LINK_BAUD, .direction = UART_DIRECTION_TXRX, .data_bits = UART_DATA_BITS_8,
        .stop_bits = UART_STOP_BITS_1, .parity = UART_PARITY_NONE, .bit_order = UART_LSB_FIRST,
        .flow_ctrl = UART_FLOWCTRL_NONE, .tx_fifo_threshold = 7, .rx_fifo_threshold = 7,
    };
    bflb_gpio_uart_init(gpio, PIN_LINK_TX, GPIO_UART_FUNC_UART0_TX);
    bflb_gpio_uart_init(gpio, PIN_LINK_RX, GPIO_UART_FUNC_UART0_RX);
    bflb_uart_init(uart0, &cfg);
    uart_on = true;
    return true;
}

/* test018: the echo runs in its own task and polls: at 2 Mbaud a byte arrives every 5 us and the RX FIFO holds 32,
 * so it may not sleep while echoing (it only yields when the line is idle). */
static void uart_echo_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!uart_echo) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int c = bflb_uart_getchar(uart0);
        if (c >= 0)
            bflb_uart_putchar(uart0, (uint8_t)c);
        else
            taskYIELD();
    }
}

static void uart_send(const char *s)
{
    while (*s)
        bflb_uart_putchar(uart0, *s++);
}
#endif

#if PT_TEST_POWER
#define RAMP_TIMEOUT_US   100000                   /* 3V3 at 90 % ~3 ms after K230_PWR (firmware/sim) */
#define FALL_TIMEOUT_US   1000000
#define OFF_SETTLE_MS     300                      /* as k230_power.c: let 1V8/3V3 discharge fully */

/* ramp time in us, 0 on timeout (the rails are switched off again) */
static uint32_t rails_on_timed(void)
{
    if (rails_on)
        return 1;
    reset_hold(true);
    uint64_t t0 = bflb_mtimer_get_time_us();
    bflb_gpio_set(gpio, PIN_K230_PWR);
    rails_on = true;
    while (sense_high() < SENSE_UP) {
        if (bflb_mtimer_get_time_us() - t0 > RAMP_TIMEOUT_US) {
            bflb_gpio_reset(gpio, PIN_K230_PWR);
            rails_on = false;
            return 0;
        }
    }
    return (uint32_t)(bflb_mtimer_get_time_us() - t0);
}

static uint32_t rails_off_timed(void)
{
    if (!rails_on)
        return 1;
#if PT_TEST_UART
    uart_stop();
#endif
    reset_hold(true);
    uint64_t t0 = bflb_mtimer_get_time_us();
    bflb_gpio_reset(gpio, PIN_K230_PWR);
    rails_on = false;
    while (sense_high() != 0) {
        if (bflb_mtimer_get_time_us() - t0 > FALL_TIMEOUT_US)
            return 0;
    }
    uint32_t t = (uint32_t)(bflb_mtimer_get_time_us() - t0);
    vTaskDelay(pdMS_TO_TICKS(OFF_SETTLE_MS));
    return t;
}
#endif

/* ---------------------------------------------------------------- I2C / AHT20 ---- */
#if PT_TEST_I2C
/* 1 if the AHT20 answered; rh/t in tenths */
static enum aht20_result aht20_read(int *rh, int *t)
{
    enum aht20_result r = aht20_start();
    return r != AHT20_OK ? r : aht20_finish(rh, t);
}

static const char *aht20_err(enum aht20_result r)
{
    return r == AHT20_ERR_I2C ? "no ACK (missing, bus stuck or 3V3_SLEEP pull-ups off)"
         : r == AHT20_ERR_BUSY ? "busy after 100 ms" : r == AHT20_ERR_CRC ? "CRC error" : "?";
}
#endif

/* ---------------------------------------------------------------- leak probes ---- */
#if PT_TEST_LEAK
#define V_SLEEP_MV 3310                            /* 3V3_SLEEP, 0.5 x (1 + 511k/91k) */
#define R_DIV      1000000.0                       /* R71 (to 3V3_SLEEP) = R72 (to GND) = 1 M */
#define R_SERIES   94000.0                         /* RB1 + RB2 (47 k each) per probe connector */

#if PT_TEST_TIMING
static uint32_t adc_t_init, adc_t_conv[6], adc_waits;   /* pt_time: the last leak_mv()'s steps, us */
static uint32_t adc_first[8];                           /* the first call since boot: init, 6 conversions, total */
static bool adc_first_restored;                         /* ... and whether it restored the stored calibration */
#endif

/*
 * test021: kept across HBN and resets in HBN RAM, placed by the linker in .hbn_noinit (firmware/bl616/
 * bl616_leakcam.ld.in; never loaded, never zeroed). HBN RAM is only reliable with its retention mode off while the
 * firmware runs: main() turns it off before the first access, pt_sleep turns it on as the very last step (see the
 * HBN RAM rule at main()).
 */
static volatile struct {
    uint32_t wake_due;                             /* RTC tick the pt_sleep wake is due at, 0 = none */
    uint32_t cal_valid;                            /* T021_CAL: the ADC calibration below is valid */
    float coe;                                     /* bflb_adc.c's calibration result after a cold boot */
    uint32_t os1;
    int32_t os2;
    uint32_t tsen;
} t021 __attribute__((section(".hbn_noinit")));
#define T021_CAL 0x43414cu                         /* "CAL" */

/* bflb_adc.c (SDK 2.3.35) keeps its calibration in these globals, and bflb_adc_init() runs the calibration (28.6 ms:
 * ~20 conversions + a 10 ms delay) only while adc_cali_complete is 0, i.e. once per reset - and every HBN wake is a
 * reset. Handing the stored result back and setting the flag skips it (as the product's leak_wake.c does). */
extern volatile float coe;
extern volatile uint32_t os1;
extern volatile int os2;
extern volatile uint32_t tsen_offset;
extern volatile int adc_cali_complete;

/* probe node in mV (ADC channel 0 = IO20, 3.2 V reference), -1 if no conversion; as leak_wake.c */
static int leak_mv(void)
{
#if PT_TEST_TIMING
    uint64_t ta = bflb_mtimer_get_time_us();
    adc_waits = 0;
#endif
    struct bflb_device_s *adc = bflb_device_get_by_name("adc");
    struct bflb_adc_config_s cfg = {
        .clk_div = ADC_CLK_DIV_32, .scan_conv_mode = false, .continuous_conv_mode = true,
        .differential_mode = false, .resolution = ADC_RESOLUTION_16B, .vref = ADC_VREF_3P2V,
    };
    struct bflb_adc_channel_s chan = { .pos_chan = ADC_CHANNEL_0, .neg_chan = ADC_CHANNEL_GND };
    uint32_t raw = 0;
    bool got = false;
    bool restored = false;
#if PT_TEST_SLEEP
    if (!adc_cali_complete && t021.cal_valid == T021_CAL) {   /* first ADC use since the reset: restore */
        coe = t021.coe;
        os1 = t021.os1;
        os2 = t021.os2;
        tsen_offset = t021.tsen;
        adc_cali_complete = 1;
        restored = true;
    }
#endif
    bool calibrated_before = adc_cali_complete;
    bflb_adc_init(adc, &cfg);
#if PT_TEST_SLEEP
    if (!calibrated_before) {                      /* it calibrated just now (cold boot): keep the result */
        t021.coe = coe;
        t021.os1 = os1;
        t021.os2 = os2;
        t021.tsen = tsen_offset;
        t021.cal_valid = T021_CAL;
    }
#endif
    bflb_adc_channel_config(adc, &chan, 1);
#if PT_TEST_TIMING
    uint64_t ts = bflb_mtimer_get_time_us();
    adc_t_init = (uint32_t)(ts - ta);
#endif
    bflb_adc_start_conversion(adc);
    for (int i = 0, waited = 0; !got && waited < 5000;) {     /* 10 us polls, as leak_wake.c (test021) */
        if (bflb_adc_get_count(adc) == 0) {
            bflb_mtimer_delay_us(10);
            waited++;
#if PT_TEST_TIMING
            adc_waits++;
#endif
            continue;
        }
        raw = bflb_adc_read_raw(adc);
#if PT_TEST_TIMING
        adc_t_conv[i] = (uint32_t)(bflb_mtimer_get_time_us() - ts);
#endif
        got = i++ == 5;                            /* the SDK's first results after a start are stale */
    }
#if PT_TEST_TIMING
    if (!adc_first[7]) {
        adc_first_restored = restored;
        adc_first[0] = adc_t_init;
        for (int k = 0; k < 6; k++)
            adc_first[k + 1] = adc_t_conv[k];
        adc_first[7] = (uint32_t)(bflb_mtimer_get_time_us() - ta);
    }
#endif
    bflb_adc_stop_conversion(adc);
    int mv = -1;
    if (got) {
        struct bflb_adc_result_s r;
        bflb_adc_parse_result(adc, &raw, &r, 1);
        mv = r.millivolt;
    }
    bflb_adc_deinit(adc);
    return mv;
}

/* resistance across a probe connector in kohm (water + nothing else), < 0 = dry (> 20 Mohm) */
static long leak_kohm(int mv)
{
    if (mv <= 0)
        return 0;
    double rx = R_DIV * mv / (V_SLEEP_MV - mv);    /* R72 || probe path */
    if (rx >= R_DIV * 0.98)
        return -1;
    double rp = 1.0 / (1.0 / rx - 1.0 / R_DIV) - R_SERIES;
    return rp > 20e6 ? -1 : (long)(rp / 1000.0 + 0.5);
}
#endif

/* ---------------------------------------------------------------- status ---- */
static const char *wake_names(unsigned w);
static unsigned last_wake;                         /* main(): HBN wake sources of this boot (WAKE_* bits) */

static bool usb_pgood_high(void)
{
    return bflb_acomp_get_result(AON_ACOMP0_ID) != 0;
}

static void print_status(bool line)
{
    unsigned n = sense_high();
    if (line) {                                    /* pt_mon: one compact line */
        printf("[%lu s]", (unsigned long)(bflb_mtimer_get_time_ms() / 1000));
#if PT_TEST_USB
        printf(" usb=%s", usb_pgood_high() ? "no" : "yes");
#endif
        printf(" pwr=%s 3v3=%u/%u rst=%s", rails_on ? "on" : "off", n, SENSE_ALL, reset_held ? "held" : "run");
#if PT_TEST_LEAK
        int mv = leak_mv();
        long k = leak_kohm(mv);
        printf(" leak=%dmV", mv);
        if (k >= 0)
            printf("/%ldk", k);
#endif
#if PT_TEST_I2C
        int rh, t;
        enum aht20_result r = aht20_read(&rh, &t);
        if (r == AHT20_OK)
            printf(" rh=%d.%d%% t=%d.%dC", rh / 10, rh % 10, t / 10, abs(t % 10));
        else
            printf(" aht20=err");
#endif
#if PT_TEST_UART
        printf(" uart=%s", uart_on ? "on" : "off");
#endif
        printf("\r\n");
        return;
    }
#if PT_TEST_USB
    printf("USB cable   : %s (PGOOD %s)\r\n", usb_pgood_high() ? "not detected" : "detected",
           usb_pgood_high() ? "high" : "low");
#endif
    printf("K230_PWR    : %s\r\n", rails_on ? "HIGH (rails commanded on)" : "low (rails off)");
    printf("K230 reset  : %s\r\n", reset_held ? "held (Q4 on)" : "released");
    printf("K230 BOOT1  : IO27 %s\r\n", boot1_name[boot1]);
    printf("3V3 switched: %s (%u of %u SDIO pull-ups high; K230_ALIVE %s)\r\n",
           n >= SENSE_UP ? "UP" : n == 0 ? "off" : "PARTIAL - check", n, SENSE_ALL,
           bflb_gpio_read(gpio, PIN_K230_ALIVE) ? "high" : "low");
    if (rails_on != (n >= SENSE_UP))
        printf("  FAIL: 3V3 does not follow K230_PWR. Check 0V8 (U6, C18) and 3V3 (U8, C87).\r\n");
#if PT_TEST_LEAK
    int mv = leak_mv();
    long k = leak_kohm(mv);
    if (k < 0)
        printf("Leak probes : %d mV, dry (expected ~1650 mV with nothing across S1..S3)\r\n", mv);
    else
        printf("Leak probes : %d mV, %ld kohm across a probe%s\r\n", mv, k, mv < 825 ? " (WET by the product threshold)" : "");
#endif
#if PT_TEST_I2C
    int rh, t;
    enum aht20_result r = aht20_read(&rh, &t);
    if (r == AHT20_OK)
        printf("AHT20       : %d.%d %%RH, %d.%d C\r\n", rh / 10, rh % 10, t / 10, abs(t % 10));
    else
        printf("AHT20       : FAIL, %s\r\n", aht20_err(r));
#endif
#if PT_TEST_UART
    printf("UART0 link  : %s\r\n", uart_on ? "on, 1 message/s" : "off");
#endif
}

#define LOCKED(code) do { xSemaphoreTake(hw, portMAX_DELAY); code; xSemaphoreGive(hw); } while (0)

static int cmd_status(int argc, char **argv)
{
    LOCKED(print_status(false));
    return 0;
}

static int cmd_mon(int argc, char **argv)
{
    monitor = argc < 2 || strcmp(argv[1], "off") != 0;
    printf("monitor %s\r\n", monitor ? "on (pt_mon off to stop)" : "off");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_status, pt_status, board test status);
SHELL_CMD_EXPORT_ALIAS(cmd_mon, pt_mon, one status line per second (pt_mon off));

/* ---------------------------------------------------------------- commands ---- */
#if PT_TEST_POWER
static int cmd_on(int argc, char **argv)
{
    uint32_t t;
    LOCKED(t = rails_on_timed());
    if (t == 0)
        printf("FAIL: 3V3 not up %u ms after K230_PWR (%u of %u lines high); rails switched off.\r\n"
               "  Measure 0V8 at C18 and 3V3 at C87 (power_check.py) and check U6/U8.\r\n",
               RAMP_TIMEOUT_US / 1000, sense_high(), SENSE_ALL);
    else
        printf("OK: 3V3 up %lu.%03lu ms after K230_PWR (expected ~3 ms). K230 held in reset.\r\n"
               "  Now measure 0V8, 3V3, 1V8, 1V1 (power_check.py flashed), then pt_run.\r\n",
               (unsigned long)(t / 1000), (unsigned long)(t % 1000));
    return 0;
}

static int cmd_off(int argc, char **argv)
{
    uint32_t t;
#if PT_TEST_SDU
    sdu_stop();                     /* the SDIO pads first: nothing may drive into the dropping 3V3 */
#endif
    LOCKED(t = rails_off_timed());
    if (t == 0)
        printf("FAIL: 3V3 still up 1 s after K230_PWR went low: something else feeds it.\r\n");
    else
        printf("OK: 3V3 gone %lu ms after K230_PWR low (expected 56-82 ms through RD1/RD2).\r\n",
               (unsigned long)(t / 1000));
    return 0;
}

static int cmd_run(int argc, char **argv)
{
    if (!rails_on) {
        printf("rails are off: pt_on first\r\n");
        return 0;
    }
    reset_hold(false);
    run_us = bflb_mtimer_get_time_us();
    printf("K230 reset released. With a blank NAND its ROM enumerates on USB-C 1 within ~2 s:\r\n"
           "  lsusb | grep 29f1:0230      (Windows: Device Manager, 'K230 USB Boot Device')\r\n"
           "That proves 0V8, 1V1, 1V8 and 3V3 together. pt_hold puts it back in reset.\r\n");
    return 0;
}

static int cmd_hold(int argc, char **argv)
{
    reset_hold(true);
    printf("K230 held in reset\r\n");
    return 0;
}

static int cmd_cycle(int argc, char **argv)
{
    unsigned n = argc > 1 ? (unsigned)atoi(argv[1]) : 10;
    uint32_t up_min = UINT32_MAX, up_max = 0, dn_min = UINT32_MAX, dn_max = 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t up, dn;
        LOCKED(up = rails_on_timed());
        if (up == 0) {
            printf("cycle %u: FAIL, 3V3 did not come up\r\n", i + 1);
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        LOCKED(dn = rails_off_timed());
        if (dn == 0) {
            printf("cycle %u: FAIL, 3V3 did not go down\r\n", i + 1);
            return 0;
        }
        up_min = up < up_min ? up : up_min, up_max = up > up_max ? up : up_max;
        dn_min = dn < dn_min ? dn : dn_min, dn_max = dn > dn_max ? dn : dn_max;
    }
    printf("OK: %u cycles, 3V3 up in %lu-%lu us, down in %lu-%lu ms. Rails left off.\r\n", n,
           (unsigned long)up_min, (unsigned long)up_max, (unsigned long)(dn_min / 1000),
           (unsigned long)(dn_max / 1000));
    return 0;
}
/* K230_ALIVE: the agent toggles it every 500 ms; the product BL616 ends a session 2 s after the
 * last edge. Busy-polled like pt_sdio watch: the shell is blocked for the watch */
static int cmd_alive(int argc, char **argv)
{
    uint32_t secs = argc > 1 ? (uint32_t)atoi(argv[1]) : 10, edges = 0;
    uint64_t t0 = bflb_mtimer_get_time_us(), last_edge = 0, first_edge = 0;
    uint32_t h_min = UINT32_MAX, h_max = 0;
    bool v = bflb_gpio_read(gpio, PIN_K230_ALIVE);
    printf("watching K230_ALIVE for %lu s, now %s\r\n", (unsigned long)secs, v ? "high" : "low");
    while (bflb_mtimer_get_time_us() - t0 < (uint64_t)secs * 1000000u) {
        bool n = bflb_gpio_read(gpio, PIN_K230_ALIVE);
        if (n == v)
            continue;
        uint64_t now = bflb_mtimer_get_time_us();
        if (edges++ == 0)
            first_edge = now;
        else {
            uint32_t h = (uint32_t)(now - last_edge);
            h_min = h < h_min ? h : h_min, h_max = h > h_max ? h : h_max;
        }
        last_edge = now, v = n;
    }
    printf("  edges %lu, now %s\r\n", (unsigned long)edges, v ? "high" : "low");
    if (edges >= 2)
        printf("  half-period min %lu / avg %lu / max %lu ms (agent: 500)\r\n", (unsigned long)(h_min / 1000),
               (unsigned long)((last_edge - first_edge) / (edges - 1) / 1000), (unsigned long)(h_max / 1000));
    if (edges && run_us && first_edge > run_us)
        printf("  first edge %lu ms after pt_run\r\n", (unsigned long)((first_edge - run_us) / 1000));
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_on, pt_on, K230 rails on (reset held) and time the 3V3 ramp);
SHELL_CMD_EXPORT_ALIAS(cmd_off, pt_off, K230 rails off and time the discharge);
SHELL_CMD_EXPORT_ALIAS(cmd_run, pt_run, release K230 reset: blank NAND enumerates as 29f1:0230);
SHELL_CMD_EXPORT_ALIAS(cmd_hold, pt_hold, hold the K230 in reset);
SHELL_CMD_EXPORT_ALIAS(cmd_cycle, pt_cycle, n power cycles with timing (default 10));
SHELL_CMD_EXPORT_ALIAS(cmd_alive, pt_alive, K230_ALIVE heartbeat for s seconds (default 10));
#endif

#if PT_TEST_UART
static int cmd_uart(int argc, char **argv)
{
    const char *a = argc > 1 ? argv[1] : "on";
    if (!strcmp(a, "off")) {
        LOCKED(uart_stop());
        printf("UART0 off, link pins parked\r\n");
    } else if (!strcmp(a, "send") && argc > 2) {
        if (!uart_on) {
            printf("UART0 is off: pt_uart on\r\n");
            return 0;
        }
        LOCKED(for (int i = 2; i < argc; i++) { uart_send(argv[i]); uart_send(i + 1 < argc ? " " : "\r\n"); });
        printf("sent\r\n");
    } else if (!strcmp(a, "echo")) {
        static bool task;
        bool ok;
        LOCKED(uart_stop(); ok = uart_start());
        if (!ok) {
            printf("3V3 is off: pt_on first\r\n");
            return 0;
        }
        if (!task)
            task = xTaskCreate(uart_echo_task, "uecho", 512, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS;
        uart_echo = true;
        printf("UART0 echo at %lu baud (pt_uart off to stop)\r\n", (unsigned long)LINK_BAUD);
    } else {
        bool ok;
        LOCKED(ok = uart_on || uart_start());
        printf(ok ? "UART0 on: IO21 TX -> K230 UART1 RX, IO22 RX <- K230 UART1 TX, LINK_BAUD 8N1, 1 message/s\r\n"
                  : "3V3 is off: pt_on first (the link pins are pulled up to the K230's 3V3)\r\n");
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_uart, pt_uart, UART0 link test: on | off | send <text> | echo);
#endif

#if PT_TEST_I2C
static int cmd_i2c(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "scan")) {
        struct bflb_device_s *i2c0 = bflb_device_get_by_name("i2c0");
        unsigned found = 0;
        printf("I2C0 scan (IO28 SCL, IO29 SDA, 4.7 k to 3V3_SLEEP):");
        xSemaphoreTake(hw, portMAX_DELAY);
        for (uint16_t a = 0x08; a < 0x78; a++) {
            uint8_t b;
            struct bflb_i2c_msg_s msg = { .addr = a, .flags = I2C_M_READ, .buffer = &b, .length = 1 };
            if (bflb_i2c_transfer(i2c0, &msg, 1) >= 0) {
                printf(" 0x%02x", a);
                found++;
            }
        }
        xSemaphoreGive(hw);
        printf("%s\r\n", found ? "" : " nothing answered");
        printf("expected: 0x38 (AHT20, U17)\r\n");
        return 0;
    }
    int rh, t;
    enum aht20_result r;
    LOCKED(r = aht20_read(&rh, &t));
    if (r == AHT20_OK)
        printf("AHT20 OK: %d.%d %%RH, %d.%d C\r\n", rh / 10, rh % 10, t / 10, abs(t % 10));
    else
        printf("AHT20 FAIL: %s\r\n", aht20_err(r));
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_i2c, pt_i2c, AHT20 reading or pt_i2c scan);
#endif

#if PT_TEST_USB
static int cmd_usb(int argc, char **argv)
{
    bool pg = usb_pgood_high();
    printf("USB cable on USB-C 1: %s (PGOOD %s, ACOMP0; plugging and unplugging is reported as it happens); this boot "
           "woke by %s\r\n", pg ? "not detected" : "detected", pg ? "high" : "low", wake_names(last_wake));
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_usb, pt_usb, USB cable detection (PGOOD));
#endif

#if PT_TEST_LEAK
/* test034: the leak comparator exactly as the product's leak_init() (leak_wake.c): ACOMP1 on ADC0 = IO20 against
 * vio_sel 17 x 50 mV = 0.85 V (the scaling factor has no effect: `pt_leak sweep`), 30 mV hysteresis each way, slowest
 * bias. Wet = node below the reference = below ~430 kohm across a probe. */
static void leak_acomp_init(void)
{
    struct bflb_acomp_config_s cfg = {
        .mux_en = ENABLE,
        .pos_chan_sel = AON_ACOMP_CHAN_ADC0,
        .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1,
        .vio_sel = 17,                                          /* 0.85 V: vio_sel x 50 mV */
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_1,           /* no effect on this input */
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
    };
    bflb_acomp_init(AON_ACOMP1_ID, &cfg);
    bflb_acomp_enable(AON_ACOMP1_ID);
    bflb_mtimer_delay_ms(1);
}

static bool leak_acomp_wet(void)
{
    return bflb_acomp_get_result(AON_ACOMP1_ID) == 0;
}


static int cmd_leak(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "sweep")) {   /* where does the comparator flip against the probe node? */
        static const struct { uint8_t sf; const char *name; } sfs[] = {
            { AON_ACOMP_SCALING_FACTOR_0P25, "0.25" }, { AON_ACOMP_SCALING_FACTOR_0P5, "0.5" },
            { AON_ACOMP_SCALING_FACTOR_0P75, "0.75" }, { AON_ACOMP_SCALING_FACTOR_1, "1" } };
        int mv;
        LOCKED(mv = leak_mv());
        printf("pt_leak sweep: node %d mV; output per vio_sel 0..63 (1 = node above the reference)\r\n", mv);
        for (unsigned k = 0; k < sizeof(sfs) / sizeof(sfs[0]); k++) {
            char line[80];
            for (unsigned v = 0; v < 64; v++) {
                struct bflb_acomp_config_s cfg = {
                    .mux_en = ENABLE, .pos_chan_sel = AON_ACOMP_CHAN_ADC0,
                    .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1, .vio_sel = (uint8_t)v,
                    .scaling_factor = sfs[k].sf, .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,
                    .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
                    .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_30MV,
                };
                bflb_acomp_init(AON_ACOMP1_ID, &cfg);
                bflb_acomp_enable(AON_ACOMP1_ID);
                bflb_mtimer_delay_ms(2);
                line[v] = bflb_acomp_get_result(AON_ACOMP1_ID) ? '1' : '0';
            }
            line[64] = 0;
            printf("  scaling %-4s: %s\r\n", sfs[k].name, line);
        }
        leak_acomp_init();                         /* back to the product setting */
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "watch")) {   /* one line per comparator change: wet / dry tests by hand */
        uint32_t s = argc > 2 ? (uint32_t)atoi(argv[2]) : 30;
        bool wet = leak_acomp_wet();
        uint64_t t0 = bflb_mtimer_get_time_ms();
        printf("pt_leak watch %lu s: comparator %s now\r\n", (unsigned long)s, wet ? "WET" : "dry");
        while (bflb_mtimer_get_time_ms() - t0 < s * 1000u) {
            bool w = leak_acomp_wet();
            if (w != wet) {
                int mv;
                /* the comparator flips while C109 is still moving: tau = (1 M || 94 k) x 100 nF = 8.6 ms on wetting,
                 * 500 k x 100 nF = 50 ms on drying; read the settled node (a first try read 692 mV for a hard short
                 * that settles at ~260 mV) */
                vTaskDelay(pdMS_TO_TICKS(w ? 50 : 250));
                LOCKED(mv = leak_mv());
                printf("%7lu ms: %s (node %d mV, %ld kohm)\r\n", (unsigned long)(bflb_mtimer_get_time_ms() - t0),
                       w ? "WET" : "dry", mv, leak_kohm(mv));
                wet = w;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        printf("pt_leak watch: end, %s\r\n", wet ? "WET" : "dry");
        return 0;
    }
    int mv;
    LOCKED(mv = leak_mv());
    long k = leak_kohm(mv);
    printf("leak node %d mV (dry ~1650, wet below 850 mV = below ~430 kohm across a probe); comparator %s; "
           "this boot woke by %s\r\n", mv, leak_acomp_wet() ? "WET" : "dry", wake_names(last_wake));
    if (k < 0)
        printf("  dry: nothing across S1..S3\r\n");
    else
        printf("  %ld kohm across a probe (put a known resistor across S1 to check: 100k reads ~100)\r\n", k);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_leak, pt_leak, leak probe node + comparator; pt_leak watch [s]);
#endif

#if PT_TEST_SDIO
/* --------------------------------------------------------------- SDIO lines ---- */
/* The SDIO wiring to K230 MMC0 without the SDIO device firmware: watch counts edges while the K230
 * probes the bus (CLK and CMD from the K230); drive puts a pattern on CMD and D0..D3 for the K230 to
 * read in its SD host's present-state register. All pins are below 32: one register read per sample */
static const uint8_t sdio_pins[] = { PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3 };
static const char *const sdio_name[] = { "CLK", "CMD", "D0", "D1", "D2", "D3" };
#define SDIO_N ((unsigned)sizeof(sdio_pins))

static void sdio_park(void)
{
    park(PIN_SD_CLK);
    for (unsigned i = 0; i < SENSE_ALL; i++)
        bflb_gpio_init(gpio, sense_pins[i], GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
}

static int cmd_sdio(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "watch") == 0) {
        uint32_t secs = argc >= 3 ? (uint32_t)atoi(argv[2]) : 10, mask = 0, edges[SDIO_N] = { 0 };
        for (unsigned i = 0; i < SDIO_N; i++) {
            bflb_gpio_init(gpio, sdio_pins[i], GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
            mask |= 1u << sdio_pins[i];
        }
        printf("watching CLK CMD D0-D3 for %lu s (start the K230 probe now)\r\n", (unsigned long)secs);
        uint32_t last = bflb_gpio_pin0_31_read(gpio) & mask;
        uint64_t t0 = bflb_mtimer_get_time_us(), loops = 0;
        while (bflb_mtimer_get_time_us() - t0 < (uint64_t)secs * 1000000u) {
            uint32_t v = bflb_gpio_pin0_31_read(gpio) & mask, ch = v ^ last;
            if (ch)
                for (unsigned i = 0; i < SDIO_N; i++)
                    edges[i] += (ch >> sdio_pins[i]) & 1u;
            last = v;
            loops++;
        }
        for (unsigned i = 0; i < SDIO_N; i++)
            printf("  %-3s edges %8lu  now %s\r\n", sdio_name[i], (unsigned long)edges[i],
                   (last >> sdio_pins[i]) & 1u ? "high" : "low");
        printf("  %lu samples/s (edges faster than half that are undercounted)\r\n", (unsigned long)(loops / (secs ? secs : 1)));
        sdio_park();
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "drive") == 0) {
        if (!rails_on) {
            printf("K230 rails off: pt_on first (driving its unpowered pads would back-power them)\r\n");
            return 0;
        }
        unsigned v = (unsigned)strtoul(argv[2], NULL, 16);      /* bit 0 CMD, bit 1 D0 .. bit 4 D3 */
        for (unsigned i = 1; i < SDIO_N; i++) {
            bflb_gpio_init(gpio, sdio_pins[i], GPIO_OUTPUT | GPIO_FLOAT | GPIO_DRV_0);
            if ((v >> (i - 1)) & 1u)
                bflb_gpio_set(gpio, sdio_pins[i]);
            else
                bflb_gpio_reset(gpio, sdio_pins[i]);
        }
        printf("driving CMD=%u D0=%u D1=%u D2=%u D3=%u (pt_sdio park to release)\r\n", v & 1, (v >> 1) & 1,
               (v >> 2) & 1, (v >> 3) & 1, (v >> 4) & 1);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "park") == 0) {
        sdio_park();
        printf("SDIO lines released (inputs, CLK parked)\r\n");
        return 0;
    }
    printf("pt_sdio watch [s] | drive <hex: bit0 CMD, bit1 D0 .. bit4 D3> | park\r\n");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_sdio, pt_sdio, SDIO wiring: watch [s] | drive <hex> | park);
#endif

#if PT_TEST_SDU
/* ---------------------------------------------------------- SDIO device echo ---- */
/* The SDK's sdio2_test as a command, for the K230 bus speed test (tests/test015): four 2 KB download
 * buffers; every block the K230 writes comes back on the upload ports (length from its first 4 bytes,
 * tag + length as in the message router header). bflb_sdio2_init() sets what the K230 driver checks at
 * attach (application running, packed read lengths, buffer size, port pointers). */
#define SDU_BUF 2048
static struct bflb_device_s *sdu;
static bool sdu_on;
static volatile uint32_t sdu_dnld, sdu_upld, sdu_crc, sdu_abort, sdu_other, sdu_resets;
static volatile uint64_t sdu_bytes;
static uint8_t sdu_buf[SDIO2_MAX_PORT_NUM][SDU_BUF] __attribute__((aligned(64)));

static void sdu_arm(void)
{
    bflb_sdio2_init(sdu, SDU_BUF);
    for (unsigned i = 0; i < SDIO2_MAX_PORT_NUM; i++) {
        bflb_sdio2_trans_desc_t d = { .buff = sdu_buf[i], .buff_len = SDU_BUF, .user_arg = NULL };
        bflb_l1c_dcache_invalidate_range(sdu_buf[i], SDU_BUF);
        bflb_sdio2_dnld_port_push(sdu, &d);
    }
}

static void sdu_irq(void *arg, uint32_t ev, bflb_sdio2_trans_desc_t *d)
{
    (void)arg;
    switch (ev) {
    case SDIO2_IRQ_EVENT_DNLD_CPL: {
        sdu_dnld++;
        bflb_l1c_dcache_invalidate_range(d->buff, d->data_len);
        const uint16_t *h = (const uint16_t *)d->buff;         /* tag, length (bytes after the 4) */
        uint32_t n = (uint32_t)h[1] + 4u;
        d->data_len = n > SDU_BUF ? SDU_BUF : n;
        sdu_bytes += d->data_len;
        bflb_sdio2_upld_port_push(sdu, d);
        break;
    }
    case SDIO2_IRQ_EVENT_UPLD_CPL:
        sdu_upld++;
        d->buff_len = SDU_BUF;
        bflb_sdio2_dnld_port_push(sdu, d);
        break;
    case SDIO2_IRQ_EVENT_SOFT_RESET:                          /* the K230's card detection resets the SDU */
        sdu_resets++;
        bflb_sdio2_deinit(sdu);
        sdu_arm();
        break;
    case SDIO2_IRQ_EVENT_ERR_CRC:
        sdu_crc++;
        break;
    case SDIO2_IRQ_EVENT_ERR_ABORT:
        sdu_abort++;
        break;
    default:
        sdu_other++;
        break;
    }
}

static void sdu_stop(void)
{
    if (!sdu_on)
        return;
    bflb_sdio2_deinit(sdu);
    sdio_park();
    sdu_on = false;
}

static int cmd_sdu(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "on") == 0) {
        if (!rails_on) {
            printf("K230 rails off: pt_on first (the SDIO pads are pulled up to its 3V3)\r\n");
            return 0;
        }
        if (!sdu_on) {
            GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_USB20_SDU);
            sdu = bflb_device_get_by_name("sdio2");
            static const uint8_t pins[] = { PIN_SD_D2, PIN_SD_D3, PIN_SD_CMD, PIN_SD_CLK, PIN_SD_D0, PIN_SD_D1 };
            for (unsigned i = 0; i < sizeof(pins); i++)       /* as the product: SDU function, pull-up, drive 0 */
                bflb_gpio_init(gpio, pins[i], GPIO_FUNC_SDU | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_0);
            sdu_dnld = sdu_upld = sdu_crc = sdu_abort = sdu_other = sdu_resets = 0;
            sdu_bytes = 0;
            sdu_arm();
            bflb_sdio2_irq_attach(sdu, sdu_irq, NULL);
            sdu_on = true;
        }
        printf("SDIO device on: echo, %u ports x %u B (now pt_run; the K230 attaches it as the BL616)\r\n",
               (unsigned)SDIO2_MAX_PORT_NUM, (unsigned)SDU_BUF);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        sdu_stop();
        printf("SDIO device off, pads parked\r\n");
        return 0;
    }
    printf("SDIO device %s: %lu blocks in, %lu out, %lu KB echoed; CRC errors %lu, aborts %lu, other %lu, resets %lu\r\n",
           sdu_on ? "on" : "off", (unsigned long)sdu_dnld, (unsigned long)sdu_upld, (unsigned long)(sdu_bytes / 1024),
           (unsigned long)sdu_crc, (unsigned long)sdu_abort, (unsigned long)sdu_other, (unsigned long)sdu_resets);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_sdu, pt_sdu, SDIO device echo for the bus speed test: on | off | (stat));
#endif

/* ---------------------------------------------------------------- tasks ---- */
/* CherryUSB's weak hook: a terminal opening the PR1 console raises DTR */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid, (void)intf;
    console_open = dtr;
}

static void background_task(void *arg)
{
    bool shown = false;
#if PT_TEST_USB
    bool pg_last = usb_pgood_high();
#endif
    uint64_t next_s = bflb_mtimer_get_time_ms() + 1000;
    for (;;) {
        if (console_open && !shown) {
            vTaskDelay(pdMS_TO_TICKS(300));
            printf("\r\nLEAKCAM BL616 board test, " __DATE__ " " __TIME__ "\r\n"
                   "commands: pt_status pt_mon pt_boot1"
#if PT_TEST_POWER
                   " pt_on pt_off pt_run pt_hold pt_cycle"
#endif
#if PT_TEST_UART
                   " pt_uart"
#endif
#if PT_TEST_I2C
                   " pt_i2c"
#endif
#if PT_TEST_SLEEP
                   " pt_sleep pt_rtc"
#endif
#if PT_TEST_TIMING
                   " pt_time"
#endif
#if PT_TEST_PINS
                   " pt_pins"
#endif
#if PT_TEST_USB
                   " pt_usb"
#endif
#if PT_TEST_LEAK
                   " pt_leak"
#endif
                   "\r\n\r\n");
            LOCKED(print_status(false));
            shown = true;
        } else if (!console_open) {
            shown = false;
        }
#if PT_TEST_USB
        bool pg = usb_pgood_high();
        if (pg != pg_last) {
            printf("USB cable %s\r\n", pg ? "unplugged (PGOOD high)" : "plugged in (PGOOD low)");
            pg_last = pg;
        }
#endif
#if PT_TEST_UART
        if (uart_on && !uart_echo) {
            int c;
            char buf[64];
            unsigned n = 0;
            while (n < sizeof(buf) - 1 && (c = bflb_uart_getchar(uart0)) >= 0)
                buf[n++] = (char)c;
            if (n) {
                buf[n] = 0;
                printf("uart rx: %s\r\n", buf);
            }
        }
#endif
        if (bflb_mtimer_get_time_ms() >= next_s) {
            next_s += 1000;
#if PT_TEST_UART
            if (uart_on && !uart_echo) {
                char msg[64];
                snprintf(msg, sizeof(msg), "LEAKCAM BL616 uart test %u, %lu ms\r\n", ++uart_count,
                         (unsigned long)bflb_mtimer_get_time_ms());
                LOCKED(uart_send(msg));
            }
#endif
            if (monitor)
                LOCKED(print_status(true));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void boot1_set(enum boot1_mode m)
{
    if (m == BOOT1_PARK) {
        park(PIN_K230_BOOT1);
    } else {
        bflb_gpio_init(gpio, PIN_K230_BOOT1, GPIO_OUTPUT | GPIO_FLOAT | GPIO_DRV_0);
        if (m == BOOT1_HIGH)
            bflb_gpio_set(gpio, PIN_K230_BOOT1);
        else
            bflb_gpio_reset(gpio, PIN_K230_BOOT1);
    }
    boot1 = m;
}

static int cmd_boot1(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "low"))
            boot1_set(BOOT1_LOW);
        else if (!strcmp(argv[1], "high"))
            boot1_set(BOOT1_HIGH);
        else if (!strcmp(argv[1], "park"))
            boot1_set(BOOT1_PARK);
        else {
            printf("usage: pt_boot1 low|high|park\r\n");
            return -1;
        }
    }
    printf("K230 BOOT1  : IO27 %s; the ROM reads it at reset (pt_hold, pt_run)\r\n", boot1_name[boot1]);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_boot1, pt_boot1, K230 BOOT1 strap: low | high | park);

#if PT_TEST_SLEEP
/* test021: the RTC tick the wake is due at (low 32 bits, 36 h wrap) in t021; main() reads the RTC first thing, so
 * the difference is ROM + boot2 + C start-up */
#define WAKE_MARK (t021.wake_due)
static uint32_t wake_due, wake_main;               /* RTC ticks; wake_due 0 = not woken by pt_sleep */
static uint64_t main_mtimer;                       /* core timer at main(): counts from the core's reset */

#define WAKE_LEAK 1u
#define WAKE_RTC  2u
#define WAKE_USB  4u
/* as the product's wake_reason_get(): the HBN interrupt states survive the wake reset until cleared */
static unsigned wake_reason_take(void)
{
    unsigned r = 0;
    if (HBN_Get_INT_State(HBN_INT_ACOMP1) == SET)
        r |= WAKE_LEAK;
    if (HBN_Get_INT_State(HBN_INT_ACOMP0) == SET)
        r |= WAKE_USB;
    if (HBN_Get_INT_State(HBN_INT_RTC) == SET)
        r |= WAKE_RTC;
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Clear_IRQ(HBN_INT_ACOMP0);
    HBN_Clear_IRQ(HBN_INT_RTC);
    return r;
}

static const char *wake_names(unsigned w)
{
    static const char *const n[8] = { "cold start / reset", "leak", "rtc", "leak+rtc", "usb", "leak+usb", "rtc+usb",
                                      "leak+rtc+usb" };
    return n[w & 7];
}

static uint64_t rtc_now(void)
{
    uint32_t lo, hi;
    HBN_Get_RTC_Timer_Val(&lo, &hi);
    return ((uint64_t)(hi & 0xff) << 32) | lo;
}

static void wake_mark_take(void)
{
    main_mtimer = bflb_mtimer_get_time_us();
    wake_main = (uint32_t)rtc_now();
    wake_due = WAKE_MARK;
    WAKE_MARK = 0;
    if (wake_main - wake_due > 2u * 32768u)        /* any reset keeps HBN RAM: a mark from an older sleep is none */
        wake_due = 0;
}

/* Y3 must run before the sleep: selected while still starting (~1 s after power-on) it stops the HBN RTC counter,
 * and it does not start in HBN, so the chip never wakes (2026-10-22, cold start). Wait until the counter moves on
 * Y3; if it never does, RC32K, so the chip still wakes. */
static bool rtc_on_xtal(void)
{
    uint32_t l0, h0, l1, h1;
    HBN_Power_On_Xtal_32K();
    HBN_32K_Sel(HBN_32K_XTAL);
    HBN_Get_RTC_Timer_Val(&l0, &h0);
    for (int ms = 0; ms < 3000; ms += 10) {
        bflb_mtimer_delay_ms(10);
        HBN_Get_RTC_Timer_Val(&l1, &h1);
        if (l1 - l0 > 100) {                     /* ~328 ticks per 10 ms once running */
            printf("pt_sleep: Y3 running after %d ms\r\n", ms);
            return true;
        }
    }
    HBN_32K_Sel(HBN_32K_RC);
    printf("pt_sleep: Y3 not running after 3 s, RTC on RC32K\r\n");
    return false;
}

/* test031: the board's sleep current. The K230 rails go off first (HBN releases every non-AON pad: R19 then holds
 * K230_PWR low, R76 the reset); the RTC moves to the Y3 crystal (as the product's rtc_use_crystal()), then HBN
 * level 0 until the RTC wakes the BL616 after <s>; the wake is a reset, the test firmware starts again. */
static int cmd_sleep(int argc, char **argv)
{
    uint32_t s = argc > 1 ? (uint32_t)atoi(argv[1]) : 60, t;
    if (s < 2 || s > 86400) {
        printf("usage: pt_sleep <2..86400 s>\r\n");
        return -1;
    }
#if PT_TEST_SDU
    sdu_stop();
#endif
    LOCKED(t = rails_off_timed());
    (void)t;
    bool y3 = rtc_on_xtal();
    /* test034: the leak comparator wakes HBN on its next edge, as the product's hbn_sleep(): wet (falling) while dry,
     * dry again (rising) while wet */
    bool wet = leak_acomp_wet();
    HBN_Clear_IRQ(HBN_INT_ACOMP1);
    HBN_Enable_AComp_IRQ(AON_ACOMP1_ID, wet ? HBN_ACOMP_INT_EDGE_POSEDGE : HBN_ACOMP_INT_EDGE_NEGEDGE);
    /* test035: USB wakes it on the next PGOOD edge, as the product's usb_arm_hbn_wake() (plug = falling) */
    bool usb = !usb_pgood_high();
    HBN_Clear_IRQ(HBN_INT_ACOMP0);
    HBN_Enable_AComp_IRQ(AON_ACOMP0_ID, usb ? HBN_ACOMP_INT_EDGE_POSEDGE : HBN_ACOMP_INT_EDGE_NEGEDGE);
    /* HBN gives GPIO16-19 hardware pulls unless disabled; on IO16/17 (Y3) they stop the oscillator and the RTC
     * never wakes the chip (2026-10-22). pm_hbn_mode_enter() does not disable them, HBN_Mode_Enter() does. */
    HBN_Hw_Pu_Pd_Cfg(DISABLE);
    printf("pt_sleep: K230 off, HBN level 0 for %lu s (RTC on %s), leak wakes it on going %s, USB on %s; unplug PR1 "
           "now for the sleep current\r\n", (unsigned long)s, y3 ? "Y3" : "RC32K", wet ? "dry" : "wet",
           usb ? "unplug" : "plug-in");
    bflb_mtimer_delay_ms(50);           /* let the USB console send the line */
    WAKE_MARK = (uint32_t)(rtc_now() + (uint64_t)s * 32768u) | 1u;  /* pm_hbn_mode_enter() reads it <= 1 tick later */
    /* the last write to HBN RAM is done: retention on now and no HBN RAM access after it (HBN RAM rule at main()) */
    HBN_Set_HRAM_Ret();
    pm_hbn_mode_enter(PM_HBN_LEVEL_0, (uint64_t)s * 32768u);
    while (1) {
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_sleep, pt_sleep, K230 off and HBN level 0 for <s> seconds (RTC wake));

/* test031/032: does the HBN RTC counter run? It counts the selected 32 kHz clock (Y3 crystal or RC32K): 32768
 * ticks per second expected. A counter that stands still never reaches the HBN wake compare value. */
static int cmd_rtc(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "xtal")) {
        HBN_Power_On_Xtal_32K();
        HBN_32K_Sel(HBN_32K_XTAL);
        printf("pt_rtc: 32K = Y3 crystal\r\n");
    } else if (argc > 1 && !strcmp(argv[1], "rc")) {
        HBN_32K_Sel(HBN_32K_RC);
        printf("pt_rtc: 32K = RC32K\r\n");
    }
    for (int i = 0; i < 3; i++) {
        uint32_t l0, h0, l1, h1;
        HBN_Get_RTC_Timer_Val(&l0, &h0);
        uint64_t t0 = bflb_mtimer_get_time_us();
        vTaskDelay(pdMS_TO_TICKS(1000));
        HBN_Get_RTC_Timer_Val(&l1, &h1);
        uint64_t t1 = bflb_mtimer_get_time_us();
        uint64_t c0 = ((uint64_t)(h0 & 0xff) << 32) | l0, c1 = ((uint64_t)(h1 & 0xff) << 32) | l1;
        printf("pt_rtc: counter %lu -> %lu: %lu ticks in %lu us (32768 expected)\r\n",   /* no %llu here */
               (unsigned long)c0, (unsigned long)c1, (unsigned long)(c1 - c0), (unsigned long)(t1 - t0));
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_rtc, pt_rtc, HBN RTC counter rate [xtal | rc]);
#endif

#if PT_TEST_TIMING
#include "bflb_mtd.h"
#include "easyflash.h"

static void min_max(uint32_t v, uint32_t *lo, uint32_t *hi)
{
    if (v < *lo)
        *lo = v;
    if (v > *hi)
        *hi = v;
}

/* test021: the always-on side's own timings, min / max over 5 runs (us, the mtimer) */
static int cmd_time(int argc, char **argv)
{
    static bool ef_up;
    uint32_t lo, hi, t;
    if (argc > 1 && !strcmp(argv[1], "hstress")) {
        /* The evidence for the HBN RAM rule at main() (2026-10-22, rev 1): for each HBN_SRAM mode, 10 000 x {write a
         * word, read it back; write an AON register (HBN_SRAM, its own value), read the word}, and 200 x {write,
         * idle 1 ms, read}. Measured: mode none 0 / 0 / 0 wrong; RET 1 / 449-747 / 0; SLP 1 / 801-1212 / 0. */
        static const char *const mname[3] = { "none", "RET", "SLP" };
        static const uint32_t mbits[3] = { 0, HBN_RETRAM_RET_MSK, HBN_RETRAM_SLP_MSK };
        volatile uint32_t *w = (volatile uint32_t *)(HBN_RAM_BASE + 0xE40);   /* free: below .hbn_noinit */
        uint32_t base = BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK);
        for (int m = 0; m < 3; m++) {
            uint32_t e1 = 0, e2 = 0, e3 = 0, n3 = 200;
            BL_WR_REG(HBN_BASE, HBN_SRAM, base | mbits[m]);
            for (uint32_t i = 0; i < 10000; i++) {
                uint32_t v = 0x9E3779B9u * (i + 1 + (uint32_t)m * 10000u);
                *w = v;
                e1 += *w != v;
                BL_WR_REG(HBN_BASE, HBN_SRAM, base | mbits[m]);
                e2 += *w != v;
            }
            for (uint32_t i = 0; i < n3; i++) {
                uint32_t v = 0x5A5A0000u | i;
                *w = v;
                bflb_mtimer_delay_us(1000);
                e3 += *w != v;
            }
            printf("hstress %s: write+read %lu of 10000 wrong, AON reg write+read %lu of 10000, 1 ms idle+read %lu of %lu\r\n",
                   mname[m], (unsigned long)e1, (unsigned long)e2, (unsigned long)e3, (unsigned long)n3);
        }
        BL_WR_REG(HBN_BASE, HBN_SRAM, base);        /* back to the run mode: retention off */
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "recal")) {     /* forget the stored ADC calibration: the next reset measures it */
        t021.cal_valid = 0;
        printf("pt_time: stored ADC calibration cleared, the next reset calibrates and stores it again\r\n");
        return 0;
    }
    if (wake_due)
        printf("wake -> main: %lu us (RTC wake due at tick %lu, main at %lu, 30.5 us per tick)\r\n",
               (unsigned long)((uint64_t)(wake_main - wake_due) * 1000000u / 32768u), (unsigned long)wake_due,
               (unsigned long)wake_main);
    else
        printf("wake -> main: no pt_sleep wake since this boot started (pt_sleep 5, then pt_time)\r\n");
    printf("core timer at main(): %lu us (ROM + boot2 + C start-up)\r\n", (unsigned long)main_mtimer);

    if (rails_on) {
        printf("K230_PWR -> 3V3 up: rails on, pt_off first\r\n");
    } else {
        LOCKED(t = rails_on_timed());
        printf("K230_PWR -> 3V3 up: %lu us (rails left on, K230 in reset)\r\n", (unsigned long)t);
    }

    lo = UINT32_MAX, hi = 0;
    for (int i = 0; i < 5; i++) {
        int rh, tc;
        uint64_t t0 = bflb_mtimer_get_time_us();
        enum aht20_result r;
        LOCKED(r = aht20_read(&rh, &tc));
        if (r != AHT20_OK) {
            printf("AHT20: %s\r\n", aht20_err(r));
            break;
        }
        min_max((uint32_t)(bflb_mtimer_get_time_us() - t0), &lo, &hi);
    }
    printf("AHT20 trigger -> data: %lu .. %lu us\r\n", (unsigned long)lo, (unsigned long)hi);

    /* when the sample is really there: status bit 7 (busy) polled every 1 ms after the trigger (datasheet: 80 ms) */
    struct bflb_device_s *i2c = bflb_device_get_by_name("i2c0");
    lo = UINT32_MAX, hi = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t trig[3] = { 0xAC, 0x33, 0x00 }, st = 0x80;
        struct bflb_i2c_msg_s w = { .addr = 0x38, .flags = I2C_M_WRITE, .buffer = trig, .length = 3 };
        struct bflb_i2c_msg_s r1 = { .addr = 0x38, .flags = I2C_M_READ, .buffer = &st, .length = 1 };
        uint64_t t0 = bflb_mtimer_get_time_us();
        bool ok;
        LOCKED(ok = bflb_i2c_transfer(i2c, &w, 1) >= 0;
               while (ok && (st & 0x80) && bflb_mtimer_get_time_us() - t0 < 150000) {
                   bflb_mtimer_delay_ms(1);
                   ok = bflb_i2c_transfer(i2c, &r1, 1) >= 0;
               });
        if (!ok || (st & 0x80)) {
            printf("AHT20 busy poll: %s\r\n", ok ? "still busy after 150 ms" : "no ACK");
            break;
        }
        min_max((uint32_t)(bflb_mtimer_get_time_us() - t0), &lo, &hi);
    }
    printf("AHT20 trigger -> busy bit clear (1 ms polls): %lu .. %lu us\r\n", (unsigned long)lo, (unsigned long)hi);

    printf("leak probe ADC (init, 6 conversions, the first 5 dropped), each run:");
    for (int i = 0; i < 5; i++) {
        uint64_t t0 = bflb_mtimer_get_time_us();
        LOCKED((void)leak_mv());
        printf(" %lu", (unsigned long)(bflb_mtimer_get_time_us() - t0));
    }
    printf(" us\r\n  last run: init %lu us, conversions at", (unsigned long)adc_t_init);
    for (int i = 0; i < 6; i++)
        printf(" %lu", (unsigned long)adc_t_conv[i]);
    printf(" us after the start, %lu 10 us waits\r\n", (unsigned long)adc_waits);
    printf("  first call since boot: init %lu us, conversions at %lu %lu %lu %lu %lu %lu us, %lu us to the stop\r\n",
           (unsigned long)adc_first[0], (unsigned long)adc_first[1], (unsigned long)adc_first[2],
           (unsigned long)adc_first[3], (unsigned long)adc_first[4], (unsigned long)adc_first[5],
           (unsigned long)adc_first[6], (unsigned long)adc_first[7]);
    printf("  first call: stored calibration %s\r\n", adc_first_restored ? "restored (no 28.6 ms calibration)"
                                                                      : "not restored: calibrated and stored");

    if (!ef_up) {
        uint64_t t0 = bflb_mtimer_get_time_us();
        bflb_mtd_init();
        easyflash_init();
        ef_up = true;
        printf("easyflash on littlefs (PSM) init: %lu us\r\n", (unsigned long)(bflb_mtimer_get_time_us() - t0));
    }
    uint8_t w[64], r[64];
    for (unsigned i = 0; i < sizeof(w); i++)
        w[i] = (uint8_t)(i * 13 + 7);
    uint32_t slo = UINT32_MAX, shi = 0, glo = UINT32_MAX, ghi = 0, bad = 0;
    for (int i = 0; i < 5; i++) {
        w[0] = (uint8_t)i;                          /* a changed value each time: a real write */
        uint64_t t0 = bflb_mtimer_get_time_us();
        ef_set_env_blob("pt_t021", w, sizeof(w));
        uint64_t t1 = bflb_mtimer_get_time_us();
        size_t n = ef_get_env_blob("pt_t021", r, sizeof(r), NULL);
        uint64_t t2 = bflb_mtimer_get_time_us();
        min_max((uint32_t)(t1 - t0), &slo, &shi);
        min_max((uint32_t)(t2 - t1), &glo, &ghi);
        bad += n != sizeof(w) || memcmp(w, r, sizeof(w));
    }
    uint64_t t0 = bflb_mtimer_get_time_us();
    ef_del_env("pt_t021");
    printf("easyflash 64 B key: set %lu .. %lu us, get %lu .. %lu us, delete %lu us, %lu of 5 read back wrong\r\n",
           (unsigned long)slo, (unsigned long)shi, (unsigned long)glo, (unsigned long)ghi,
           (unsigned long)(bflb_mtimer_get_time_us() - t0), (unsigned long)bad);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_time, pt_time, test021 timings: wake to main + 3V3 ramp + AHT20 + probe ADC + easyflash key);
#endif

#if PT_TEST_PINS
/* test033: every GPIO's live configuration straight from GLB_GPIO_CFGn (0x200008C4 + 4 n): what the firmware really
 * set, not what the code meant to set. One parseable line per pin for tools/power-bringup/pin_check.py. */
static const char *pin_func_name(unsigned f)
{
    switch (f) {
    case 0: return "SDH";
    case 5: return "I2C0";
    case 7: return "UART";
    case 10: return "ANALOG";
    case 11: return "GPIO";
    case 12: return "SDU";
    case 26: return "JTAG";
    default: return "other";
    }
}

static int cmd_pins(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* BL616 UART signal matrix: signal = pin % 12, its 4-bit function in GLB_UART_CFG1 (0x154, signals 0-7) or
     * GLB_UART_CFG2 (0x158, 8-11), as bflb_gpio_uart_init() writes it */
    uint64_t uart_sig = getreg32(GLB_BASE + 0x154) | ((uint64_t)getreg32(GLB_BASE + 0x158) << 32);
    /* levels: the SDK drives outputs in set/clear mode (bit 30), where neither bit 24 nor GLB_GPIO_CFG136 shows the
     * output; so the pad itself is read: GLB_GPIO_CFG128/129 (input values), for an output pin with its input buffer
     * switched on for the read (ie, bit 0) - the pad level, i.e. what the pin really drives */
    uint64_t in = getreg32(GLB_BASE + 0xAC4) | ((uint64_t)getreg32(GLB_BASE + 0xAC8) << 32);
    for (unsigned pin = 0; pin <= 34; pin++) {
        uint32_t c = getreg32(GLB_BASE + 0x8C4 + pin * 4);
        unsigned f = (c >> 8) & 0x1f;
        unsigned pad = (unsigned)((in >> pin) & 1);
        if ((c >> 6) & 1 && !(c & 1)) {              /* output without input buffer: switch it on for one read */
            uintptr_t flag = bflb_irq_save();
            putreg32(c | 1, GLB_BASE + 0x8C4 + pin * 4);
            bflb_mtimer_delay_us(2);
            pad = (getreg32(GLB_BASE + (pin < 32 ? 0xAC4 : 0xAC8)) >> (pin & 31)) & 1;
            putreg32(c, GLB_BASE + 0x8C4 + pin * 4);
            bflb_irq_restore(flag);
        }
        printf("pin %02u raw %08lx func %-6s ie %lu oe %lu pu %lu pd %lu drv %lu smt %lu pad %u",
               pin, (unsigned long)c, pin_func_name(f), (unsigned long)(c & 1), (unsigned long)((c >> 6) & 1),
               (unsigned long)((c >> 4) & 1), (unsigned long)((c >> 5) & 1), (unsigned long)((c >> 2) & 3),
               (unsigned long)((c >> 1) & 1), pad);
        if (f == 7)
            printf(" uart_sig %u = func %lu", pin % 12, (unsigned long)((uart_sig >> ((pin % 12) * 4)) & 0xf));
        printf("\r\n");
    }
    printf("pins: K230_PWR %s (firmware), reset %s (firmware), 3V3 sense %u of %u\r\n", rails_on ? "on" : "off",
           reset_held ? "held" : "released", sense_high(), (unsigned)SENSE_ALL);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_pins, pt_pins, test033: every GPIO configuration and level);
#endif

/* test035: PGOOD (BQ24072, open drain, R23 100 k to 3V3_SLEEP; low = USB power good) as the product reads it:
 * IO03 analog, ACOMP0 on ADC3 against vio_sel 33 x 50 mV = 1.65 V (mid-swing; the scaling factor has no effect,
 * tests/test034), 50 mV hysteresis. Output 1 = PGOOD above 1.65 V = no USB power. */
static void usb_acomp_init(void)
{
    bflb_gpio_init(gpio, PIN_USB_PGOOD, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
    struct bflb_acomp_config_s cfg = {
        .mux_en = ENABLE,
        .pos_chan_sel = AON_ACOMP_CHAN_ADC3,                    /* GPIO3 */
        .neg_chan_sel = AON_ACOMP_CHAN_VIO_X_SCALING_FACTOR_1,
        .vio_sel = 33,
        .scaling_factor = AON_ACOMP_SCALING_FACTOR_1,
        .bias_prog = AON_ACOMP_BIAS_POWER_MODE1,
        .hysteresis_pos_volt = AON_ACOMP_HYSTERESIS_VOLT_50MV,
        .hysteresis_neg_volt = AON_ACOMP_HYSTERESIS_VOLT_50MV,
    };
    bflb_acomp_init(AON_ACOMP0_ID, &cfg);
    bflb_acomp_enable(AON_ACOMP0_ID);
    bflb_mtimer_delay_ms(1);
}

static void pins_init(void)
{
    gpio = bflb_device_get_by_name("gpio");
    /* reset asserted before K230_PWR becomes an output, rails off (as k230_power.c) */
    bflb_gpio_init(gpio, PIN_K230_RSTN, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    reset_hold(true);
    bflb_gpio_init(gpio, PIN_K230_PWR, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    bflb_gpio_reset(gpio, PIN_K230_PWR);
    bflb_gpio_init(gpio, PIN_K230_ALIVE, GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
    usb_acomp_init();                /* IO03 analog, PGOOD watched by ACOMP0 as the product's usb_sense_init() */
    park(PIN_LINK_TX);
    park(PIN_LINK_RX);
    park(PIN_SD_CLK);
    boot1_set(BOOT1_LOW);
    park(PIN_LEAK_SENS);           /* 1.65 V dry: between VIL and VIH, analog only */
    for (unsigned i = 0; i < SENSE_ALL; i++)
        bflb_gpio_init(gpio, sense_pins[i], GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN);
#if PT_TEST_UART
    uart0 = bflb_device_get_by_name("uart0");
#endif
#if PT_TEST_I2C
    aht20_init();
#endif
#if PT_TEST_LEAK
    leak_acomp_init();
#endif
}

int main(void)
{
#if PT_TEST_SLEEP
    /*
     * HBN RAM rule (tests/test021, 2026-10-22, rev 1): while HBN_SRAM has its retention (RET, bit 6) or sleep (SLP,
     * bit 7) mode set, an HBN RAM access right after any AON (HBN) register write returns or stores corrupted data
     * (4-12 % of them, `pt_time hstress`); with both bits clear, none of 30 000. The mode is an AON register: it
     * survives the HBN wake and every reset, and the ROM, boot2 and board_init() write AON registers. So: both bits
     * off here, before the first HBN RAM access, and RET on again only as the very last step before HBN (pt_sleep).
     * The product does the same in aon_init() / aon_prepare_sleep().
     */
    BL_WR_REG(HBN_BASE, HBN_SRAM, BL_RD_REG(HBN_BASE, HBN_SRAM) & ~(HBN_RETRAM_RET_MSK | HBN_RETRAM_SLP_MSK));
    wake_mark_take();      /* before board_init: the wake latency ends here */
    last_wake = wake_reason_take();
#endif
    board_init();          /* also starts the USB CDC console and its shell (defconfig) */
    pins_init();
    hw = xSemaphoreCreateMutex();
    xTaskCreate(background_task, "pt", 2048, NULL, 2, NULL);
    vTaskStartScheduler();
    while (1) {
    }
}
