#include "k230_session.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include "bflb_uart.h"
#include "hardware/uart_reg.h"

#include "board_pins.h"
#include "crash_rec.h"
#include "k230_power.h"
#include "mem_stat.h"
#include "usb_console2.h"

#define BOOT_TOGGLE_MS   400u    /* K230_ALIVE is JTAG_TCK during the K230's boot: edges before this are not ours */
#define FIRST_EDGE_MS    5000u   /* the program starts ~0.36 s after the run (test113): no heartbeat by then = failed */
#define HEARTBEAT_STOP_MS 2000u  /* the program toggles every 500 ms */
#define WAKE_FIELDS      ",0,-1,0,-1,-1"   /* no wall clock in setup, no AHT20/probe/battery reading: "none" */
#define QUEUE            6

/* the K230 console and the frames arrive by interrupt (the powertest's rule, 2026-10-24: polled, the 32-byte RX FIFO
 * overflows in 160 us at 2 Mbaud); the session task drains the ring */
#define RX_RING 4096u
static uint8_t rx_ring[RX_RING];
static volatile uint32_t rx_head, rx_tail, rx_dropped, rx_fifo_err, tx_bytes, tx_fail;
static struct bflb_device_s *uart0;
static bool uart_on;
static TaskHandle_t task;

enum state { OFF, BOOT, RUN };
static volatile enum state state;
static char reason[16];
static void (*cmd_fn)(const struct link_msg *m);
static volatile const char *stop_why;
static volatile bool stop_asked;         /* k230_session_stop(): an end the caller wanted, not a failure */
static uint64_t t_on, t_run, t_ready, first_edge, last_edge;
static unsigned edges;
static bool level;

static char queue[QUEUE][LINK_MAX_LINE];
static unsigned q_head, q_count;
static bool q_sending;

static void uart_rx_isr(int irq, void *arg)
{
    (void)irq;
    (void)arg;
    if (bflb_uart_get_intstatus(uart0) & UART_INTSTS_RTO)
        bflb_uart_int_clear(uart0, UART_INTCLR_RTO);
    while (bflb_uart_rxavailable(uart0)) {
        uint8_t c = (uint8_t)bflb_uart_getchar(uart0);
        uint32_t next = (rx_head + 1) % RX_RING;
        if (next == rx_tail)
            rx_dropped++;
        else {
            rx_ring[rx_head] = c;
            rx_head = next;
        }
    }
    uint32_t f0 = getreg32(uart0->reg_base + UART_FIFO_CONFIG_0_OFFSET);
    if (f0 & UART_RX_FIFO_OVERFLOW) {
        rx_fifo_err++;
        putreg32(f0 | UART_RX_FIFO_CLR, uart0->reg_base + UART_FIFO_CONFIG_0_OFFSET);
    }
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void uart_start(void)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    struct bflb_uart_config_s cfg = {
        .baudrate = LINK_BAUD, .direction = UART_DIRECTION_TXRX, .data_bits = UART_DATA_BITS_8,
        .stop_bits = UART_STOP_BITS_1, .parity = UART_PARITY_NONE, .bit_order = UART_LSB_FIRST,
        .flow_ctrl = UART_FLOWCTRL_NONE, .tx_fifo_threshold = 7, .rx_fifo_threshold = 7,
    };
    bflb_gpio_uart_init(gpio, PIN_LINK_TX, GPIO_UART_FUNC_UART0_TX);
    bflb_gpio_uart_init(gpio, PIN_LINK_RX, GPIO_UART_FUNC_UART0_RX);
    bflb_uart_init(uart0, &cfg);
    rx_head = rx_tail = rx_dropped = rx_fifo_err = tx_bytes = tx_fail = 0;
    bflb_uart_rxint_mask(uart0, false);
    bflb_irq_attach(uart0->irq_num, uart_rx_isr, NULL);
    bflb_irq_enable(uart0->irq_num);
    uart_on = true;
}

static void uart_stop(void)
{
    if (!uart_on)
        return;
    uart_on = false;
    bflb_uart_rxint_mask(uart0, true);
    bflb_irq_disable(uart0->irq_num);
    bflb_uart_deinit(uart0);                     /* before the rails go: TX no longer drives the pad high */
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");
    /* parked analog before the rails go: a TX pad left high back-feeds the K230's 3V3 through its RX (the 3V3 did not
     * fall in 1 s, 2026-11-05) */
    bflb_gpio_init(gpio, PIN_LINK_TX, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
    bflb_gpio_init(gpio, PIN_LINK_RX, GPIO_ANALOG | GPIO_FLOAT | GPIO_DRV_0);
}

void link_tx(const char *p, unsigned n)
{
    if (!uart_on)
        return;
    if (bflb_uart_put(uart0, (uint8_t *)p, n) < 0)
        tx_fail++;
    else
        tx_bytes += n;
}

/* the K230's console: to the second USB port, and its program's "FATAL: <words>" line into the crash record (the K230
 * has no recorder of its own yet; product application standard) */
static char con_line[96];
static unsigned con_len;
static bool fatal_seen;                  /* the K230 named its failure: that record stays, not "heartbeat stopped" */

void link_text(const char *p, unsigned n)
{
    usb_k230_write(p, n);
    for (unsigned i = 0; i < n; i++) {
        if (p[i] != '\n' && p[i] != '\r') {
            if (con_len < sizeof(con_line) - 1)
                con_line[con_len++] = p[i];
            continue;
        }
        con_line[con_len] = 0;
        const char *f = strstr(con_line, "FATAL: ");
        if (f) {
            crash_rec_k230(f + 7);
            fatal_seen = true;
        }
        con_len = 0;
    }
}

/* what the host types on the K230 console port (/dev/ttyACM1) */
void usb_k230_rx(const uint8_t *data, uint32_t len)
{
    if (uart_on)
        bflb_uart_put(uart0, (uint8_t *)data, len);
}

static uint32_t ms_since(uint64_t t)
{
    return (uint32_t)(bflb_mtimer_get_time_ms() - t);
}

static void log_mem(const char *when)
{
    struct mem_stat m;
    mem_stat(&m);
    printf("k230: %s: mem free %u min %u blocks %lu tasks %u deleted %u\r\n", when, (unsigned)m.free_bytes,
           (unsigned)m.min_free, (unsigned long)m.blocks, m.tasks, m.deleted);
}

static void off(const char *why)
{
    uart_stop();
    uint32_t fall = k230_power_off();
    state = OFF;
    printf("k230: off (%s) %lu ms after power-on: READY at %ld ms, %u heartbeat edges, 3V3 fall %lu us, rx dropped %lu,"
           " fifo overflows %lu, tx %lu B, tx failed %lu\r\n", why, (unsigned long)ms_since(t_on), t_ready ? (long)(t_ready - t_run) : -1L, edges,
           (unsigned long)fall, (unsigned long)rx_dropped, (unsigned long)rx_fifo_err, (unsigned long)tx_bytes,
           (unsigned long)tx_fail);
    log_mem("session end");
}

static void on_cmd(const struct link_msg *m)
{
    if (!strcmp(m->cmd, "READY")) {
        t_ready = bflb_mtimer_get_time_ms();
        printf("k230: READY %lu ms after the run\r\n", (unsigned long)(t_ready - t_run));
        char arg[32];
        snprintf(arg, sizeof(arg), "%s%s", reason, WAKE_FIELDS);
        q_sending = false;                       /* a restarted program: what was in flight was for the old one */
        if (!link_send_cmd("WAKE", arg))
            printf("k230: WAKE not sent: a command in flight\r\n");
        return;
    }
    if (state == RUN && cmd_fn)
        cmd_fn(m);
}

static void heartbeat(void)
{
    bool l = k230_alive();
    uint64_t now = bflb_mtimer_get_time_ms();
    if (l != level) {
        level = l;
        if (now - t_run >= BOOT_TOGGLE_MS) {
            if (!edges++) {
                first_edge = now;
                printf("k230: first heartbeat edge %lu ms after the run\r\n", (unsigned long)(now - t_run));
            }
            last_edge = now;
        }
    }
    if (!edges && now - t_run > FIRST_EDGE_MS)
        stop_why = "no heartbeat: the program did not start";
    else if (edges && now - last_edge > HEARTBEAT_STOP_MS)
        stop_why = "heartbeat stopped: the program ended";
}

static void session_task(void *arg)
{
    (void)arg;
    char buf[128];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, state == OFF ? portMAX_DELAY : pdMS_TO_TICKS(10));
        if (state == OFF)
            continue;
        unsigned n = 0;
        while (rx_tail != rx_head) {
            buf[n++] = (char)rx_ring[rx_tail];
            rx_tail = (rx_tail + 1) % RX_RING;
            if (n == sizeof(buf)) {
                link_rx(buf, n, on_cmd);
                n = 0;
            }
        }
        if (n)
            link_rx(buf, n, on_cmd);
        const struct link_result *r = link_service(bflb_mtimer_get_time_ms());
        if (r && !strcmp(r->cmd, "WAKE")) {
            if (r->end == LINK_ACKED) {
                state = RUN;
                printf("k230: WAKE %s taken %lu ms after the run\r\n", reason, (unsigned long)ms_since(t_run));
            } else
                stop_why = "WAKE not answered";
        } else if (r && !strcmp(r->cmd, "USB")) {
            if (r->end != LINK_ACKED)
                stop_why = "a USB line not answered: link down";
            taskENTER_CRITICAL();
            q_head = (q_head + 1) % QUEUE;       /* sent (or the session ends anyway) */
            q_count--;
            q_sending = false;
            taskEXIT_CRITICAL();
        }
        if (state == RUN && !q_sending && q_count) {
            char line[LINK_MAX_LINE];
            taskENTER_CRITICAL();
            memcpy(line, queue[q_head], sizeof(line));
            taskEXIT_CRITICAL();
            q_sending = link_send_cmd("USB", line);
        }
        heartbeat();
        if (!k230_power_is_on()) {               /* `k230 off` (flash_nand.sh): an end asked for, no failure record */
            stop_asked = true;
            stop_why = "switched off by `k230 off`";
        }
        if (stop_why) {
            const char *why = (const char *)stop_why;
            bool asked = stop_asked;
            stop_why = NULL;
            stop_asked = false;
            if (!asked && !fatal_seen)
                crash_rec_k230(why);             /* the session failed: in the record, sent once Wi-Fi is up */
            off(why);
        }
    }
}

void k230_session_init(void)
{
    uart0 = bflb_device_get_by_name("uart0");
    if (xTaskCreate(session_task, "k230", 2048, NULL, 12, &task) != pdPASS)
        crash_rec_fatal("k230 session task not created");
}

void k230_session_start(const char *why, void (*fn)(const struct link_msg *m))
{
    if (state != OFF)
        return;
    if (k230_power_is_on()) {               /* `k230 usbboot`: the K230 is being flashed */
        printf("k230: on for flashing (k230 usbboot): no %s session\r\n", why);
        return;
    }
    snprintf(reason, sizeof(reason), "%s", why);
    cmd_fn = fn;
    stop_why = NULL;
    stop_asked = false;
    con_len = 0;
    fatal_seen = false;
    edges = 0;
    t_ready = 0;
    q_head = q_count = 0;
    q_sending = false;
    log_mem("session start");
    t_on = bflb_mtimer_get_time_ms();
    uint32_t ramp = k230_power_on();
    if (!ramp) {
        crash_rec_k230("3V3 did not come up");
        return;
    }
    uart_start();
    link_reset();
    level = k230_alive();
    t_run = bflb_mtimer_get_time_ms();
    k230_power_run();
    state = BOOT;
    printf("k230: session %s: rails on (3V3 in %lu us), running\r\n", reason, (unsigned long)ramp);
    xTaskNotifyGive(task);
}

void k230_session_stop(const char *why)
{
    if (state != OFF) {
        stop_asked = true;
        stop_why = why;
        xTaskNotifyGive(task);
    }
}

bool k230_session_active(void)
{
    return state != OFF;
}

bool k230_session_usb_line(const char *line)
{
    bool ok = false;
    taskENTER_CRITICAL();
    if (q_count < QUEUE) {
        snprintf(queue[(q_head + q_count) % QUEUE], LINK_MAX_LINE, "%s", line);
        q_count++;
        ok = true;
    }
    taskEXIT_CRITICAL();
    if (!ok)
        printf("k230: USB line queue full: a line for the phone dropped\r\n");
    xTaskNotifyGive(task);
    return ok;
}
