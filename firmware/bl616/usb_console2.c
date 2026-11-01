/*
 * LEAKCAM USB console with two CDC ACM ports on one USB device (ffff:ffff), replacing the SDK's single-port
 * bsp/common/usb_app/usb_console.c (SDK 2.3.35, this file is its copy with a second port; CMakeLists.txt swaps it in).
 *
 *   port 0 (interfaces 0/1, EP 0x83 / 0x04 / 0x85): the BL616 shell and printf, as before   -> /dev/ttyACM0
 *   port 1 (interfaces 2/3, EP 0x81 / 0x02 / 0x86): the K230 console, bridged over the       -> /dev/ttyACM1
 *          K230 <-> BL616 UART (k230_console.c): usb_k230_write() sends to the host, usb_k230_rx() receives
 *
 * Why: the K230's own console (CH340 on USB-C 2) lives on the K230's switched 3V3, re-enumerates with every K230
 * power cycle and needs a VMware "connect" each time, which VMware stops offering (bug-002, tests/test038). One USB
 * device with two ports is one click for both, and the udev rule (ffff:ffff) opens both (user, 2026-10-22).
 */
#include "usb_console.h"
#include "usb_console2.h"

#include <stdbool.h>
#include <stdint.h>

#include "device_id.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "bflb_irq.h"
#include "board.h"
#include "ring_buffer.h"
#include "shell.h"
#include "usbd_cdc_acm.h"
#include "usbd_core.h"

#define USB_CONSOLE_BUS_ID         0U
#define USB_CONSOLE_FS_MPS         64U
#define USB_CONSOLE_HS_MPS         512U
#define USB_CONSOLE_PORTS          2U
#define USB_CONSOLE_CONFIG_SIZE    (9U + USB_CONSOLE_PORTS * CDC_ACM_DESCRIPTOR_LEN)
#define USB_CONSOLE_EVENT_IDLE     USBD_EVENT_UNKNOWN

/* port 0 keeps the SDK's endpoints; port 1 takes free ones */
#define P0_IN_EP  0x83U
#define P0_OUT_EP 0x04U
#define P0_INT_EP 0x85U
#define P1_IN_EP  0x81U
#define P1_OUT_EP 0x02U
#define P1_INT_EP 0x86U

#ifndef CONFIG_CONSOLE_USB_CDC_VID
#define CONFIG_CONSOLE_USB_CDC_VID 0xFFFFU
#endif
#ifndef CONFIG_CONSOLE_USB_CDC_PID
#define CONFIG_CONSOLE_USB_CDC_PID 0xFFFFU
#endif
#ifndef CONFIG_CONSOLE_USB_CDC_TX_RING_SIZE
#define CONFIG_CONSOLE_USB_CDC_TX_RING_SIZE 4096U
#endif
#ifndef CONFIG_CONSOLE_USB_CDC_TASK_STACK_SIZE
#define CONFIG_CONSOLE_USB_CDC_TASK_STACK_SIZE 1024U
#endif
#ifndef CONFIG_CONSOLE_USB_CDC_TX_FLUSH_TIMEOUT_MS
#define CONFIG_CONSOLE_USB_CDC_TX_FLUSH_TIMEOUT_MS 5U
#endif
/* the K230 console: the link rate's boot log arrives faster than USB drains it in bursts, so a larger ring */
#define K230_TX_RING_SIZE 8192U

#define USB_CONSOLE_RETRY_DELAY_MS 10U

static const uint8_t usb_console_device_descriptor[] = { USB_DEVICE_DESCRIPTOR_INIT(
    USB_2_0, 0xEF, 0x02, 0x01, CONFIG_CONSOLE_USB_CDC_VID, CONFIG_CONSOLE_USB_CDC_PID, 0x0100, 0x01) };

#define USB_CONSOLE_CONFIG_BODY(mps)                                                                                 \
    CDC_ACM_DESCRIPTOR_INIT(0x00, P0_INT_EP, P0_OUT_EP, P0_IN_EP, mps, 0x04),                                          \
    CDC_ACM_DESCRIPTOR_INIT(0x02, P1_INT_EP, P1_OUT_EP, P1_IN_EP, mps, 0x05)

#ifdef CONFIG_USB_HS
static const uint8_t usb_console_hs_config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONSOLE_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, 100),
    USB_CONSOLE_CONFIG_BODY(USB_CONSOLE_HS_MPS)
};
static const uint8_t usb_console_other_hs_config_descriptor[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONSOLE_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, 100),
    USB_CONSOLE_CONFIG_BODY(USB_CONSOLE_HS_MPS)
};
#endif

static const uint8_t usb_console_fs_config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONSOLE_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, 100),
    USB_CONSOLE_CONFIG_BODY(USB_CONSOLE_FS_MPS)
};

#ifdef CONFIG_USB_HS
static const uint8_t usb_console_other_fs_config_descriptor[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONSOLE_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, 100),
    USB_CONSOLE_CONFIG_BODY(USB_CONSOLE_FS_MPS)
};
static const uint8_t usb_console_device_qualifier_descriptor[] = { USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(
    USB_2_0, 0xEF, 0x02, 0x01, 0x01) };
#endif

/* string 2 = the product, 3 = the serial number (the device ID, device_id.c), 4 = port 0's interface name, 5 = port
 * 1's (shown by Linux in /dev/serial/by-id) */
static const char *usb_console_string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },
    "Bouffalo Lab",
    "Bouffalo Lab USB Console",
    NULL,                                    /* the serial number: device_id() */
    "LEAKCAM BL616 console",
    "LEAKCAM K230 console",
};

static const uint8_t *usb_console_device_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return usb_console_device_descriptor;
}

static const uint8_t *usb_console_config_descriptor_cb(uint8_t speed)
{
#ifdef CONFIG_USB_HS
    if (speed == USB_SPEED_HIGH) {
        return usb_console_hs_config_descriptor;
    }
#else
    (void)speed;
#endif
    return usb_console_fs_config_descriptor;
}

#ifdef CONFIG_USB_HS
static const uint8_t *usb_console_device_qualifier_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return usb_console_device_qualifier_descriptor;
}

static const uint8_t *usb_console_other_speed_descriptor_cb(uint8_t speed)
{
    return speed == USB_SPEED_HIGH ? usb_console_other_fs_config_descriptor : usb_console_other_hs_config_descriptor;
}
#endif

static const char *usb_console_string_descriptor_cb(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(usb_console_string_descriptors) / sizeof(usb_console_string_descriptors[0]))) {
        return NULL;
    }
    return index == 3 ? device_id() : usb_console_string_descriptors[index];
}

static const struct usb_descriptor usb_console_descriptor = {
    .device_descriptor_callback = usb_console_device_descriptor_cb,
    .config_descriptor_callback = usb_console_config_descriptor_cb,
#ifdef CONFIG_USB_HS
    .device_quality_descriptor_callback = usb_console_device_qualifier_descriptor_cb,
    .other_speed_descriptor_callback = usb_console_other_speed_descriptor_cb,
#endif
    .string_descriptor_callback = usb_console_string_descriptor_cb,
};

enum usb_console_state {
    USB_CONSOLE_STA_WAIT_USBD_CFG = 0,
    USB_CONSOLE_STA_DATA_POLLING,
    USB_CONSOLE_STA_SUSPENDED,
};

enum port_tx_state {
    TX_IDLE = 0,
    TX_WAIT_USBD_IN,
};

enum port_rx_state {
    RX_START_USBD_OUT = 0,
    RX_WAIT_USBD_OUT,
};

/* one CDC ACM port: the SDK console's per-port state, unchanged, twice */
struct port {
    Ring_Buffer_Type tx_ring;
    uint8_t *tx_buffer;                 /* USB_NOCACHE, USB_CONSOLE_HS_MPS */
    uint8_t *rx_buffer;
    uint8_t ep_in, ep_out;
    enum port_tx_state tx_state;
    enum port_rx_state rx_state;
    uint32_t tx_len;
    uint32_t rx_done_len;
    bool tx_done_pending;
    bool rx_done_pending;
    void (*rx)(const uint8_t *data, uint32_t len);
};

static struct {
    SemaphoreHandle_t event_sem;
    volatile enum usb_console_state state;
    uint8_t usb_event;
} usb_console = {
    .usb_event = USB_CONSOLE_EVENT_IDLE,
};

static void p0_rx(const uint8_t *data, uint32_t len)
{
    shell_exe_cmd((uint8_t *)data, (uint16_t)len);
}

__attribute__((weak)) void usb_k230_rx(const uint8_t *data, uint32_t len)
{
    (void)data;
    (void)len;
}

static uint8_t p0_tx_pool[CONFIG_CONSOLE_USB_CDC_TX_RING_SIZE];
static uint8_t p1_tx_pool[K230_TX_RING_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t p0_tx_buffer[512];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t p0_rx_buffer[512];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t p1_tx_buffer[512];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t p1_rx_buffer[512];

static struct port ports[USB_CONSOLE_PORTS] = {
    { .tx_buffer = p0_tx_buffer, .rx_buffer = p0_rx_buffer, .ep_in = P0_IN_EP, .ep_out = P0_OUT_EP, .rx = p0_rx },
    { .tx_buffer = p1_tx_buffer, .rx_buffer = p1_rx_buffer, .ep_in = P1_IN_EP, .ep_out = P1_OUT_EP, .rx = NULL },
};

static struct usbd_interface intf[2 * USB_CONSOLE_PORTS];

static void bulk_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void bulk_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes);

static struct usbd_endpoint endpoints[2 * USB_CONSOLE_PORTS] = {
    { .ep_addr = P0_IN_EP, .ep_cb = bulk_in_cb },
    { .ep_addr = P0_OUT_EP, .ep_cb = bulk_out_cb },
    { .ep_addr = P1_IN_EP, .ep_cb = bulk_in_cb },
    { .ep_addr = P1_OUT_EP, .ep_cb = bulk_out_cb },
};

static struct port *port_of_ep(uint8_t ep)
{
    for (unsigned i = 0; i < USB_CONSOLE_PORTS; i++)
        if (ports[i].ep_in == ep || ports[i].ep_out == ep)
            return &ports[i];
    return NULL;
}

static void notify_task(void)
{
    if (usb_console.event_sem == NULL) {
        return;
    }
    if (xPortIsInsideInterrupt()) {
        BaseType_t woken = pdFALSE;
        (void)xSemaphoreGiveFromISR(usb_console.event_sem, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        (void)xSemaphoreGive(usb_console.event_sem);
    }
}

static void data_reset(enum usb_console_state next_state)
{
    uintptr_t flags = bflb_irq_save();
    usb_console.state = next_state;
    for (unsigned i = 0; i < USB_CONSOLE_PORTS; i++) {
        struct port *p = &ports[i];
        Ring_Buffer_Reset(&p->tx_ring);
        p->tx_done_pending = false;
        p->rx_done_pending = false;
        p->tx_len = 0U;
        p->rx_done_len = 0U;
        p->tx_state = TX_IDLE;
        p->rx_state = RX_START_USBD_OUT;
    }
    bflb_irq_restore(flags);
}

static ssize_t port_write(struct port *p, const void *data, size_t size)
{
    bool should_notify;
    uint32_t length_before, written;
    uintptr_t flags;

    if (size == 0U) {
        return 0;
    }
    if (data == NULL) {
        return -1;
    }
    flags = bflb_irq_save();
    if ((usb_console.state != USB_CONSOLE_STA_DATA_POLLING) || (size > Ring_Buffer_Get_Empty_Length(&p->tx_ring))) {
        bflb_irq_restore(flags);
        return (ssize_t)size;           /* as the SDK: dropped while the host is away or the ring is full */
    }
    length_before = Ring_Buffer_Get_Length(&p->tx_ring);
    written = Ring_Buffer_Write(&p->tx_ring, data, (uint32_t)size);
    should_notify = (length_before == 0U) ||
                    ((length_before < 512U) && (written >= (512U - length_before)));
    bflb_irq_restore(flags);
    if (should_notify) {
        notify_task();
    }
    return (ssize_t)size;
}

ssize_t usb_console_write(const void *data, size_t size)
{
    return port_write(&ports[0], data, size);
}

ssize_t usb_k230_write(const void *data, size_t size)
{
    return port_write(&ports[1], data, size);
}

static void bulk_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)nbytes;
    struct port *p = port_of_ep(ep);
    if (busid != USB_CONSOLE_BUS_ID || p == NULL) {
        return;
    }
    uintptr_t flags = bflb_irq_save();
    p->tx_done_pending = true;
    bflb_irq_restore(flags);
    notify_task();
}

static void bulk_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    struct port *p = port_of_ep(ep);
    if (busid != USB_CONSOLE_BUS_ID || p == NULL) {
        return;
    }
    uintptr_t flags = bflb_irq_save();
    p->rx_done_len = nbytes;
    p->rx_done_pending = true;
    bflb_irq_restore(flags);
    notify_task();
}

static void event_cb(uint8_t busid, uint8_t event)
{
    if (busid != USB_CONSOLE_BUS_ID) {
        return;
    }
    uintptr_t flags = bflb_irq_save();
    usb_console.usb_event = event;
    bflb_irq_restore(flags);
    notify_task();
}

/* the SDK's TX state machine, per port */
static bool tx_polling(struct port *p, bool flush_timeout, TickType_t *wait_ticks, bool *flush_waiting)
{
    bool done;
    uint32_t pending_len;
    TickType_t flush_ticks;
    uintptr_t flags;

polling_continue:
    switch (p->tx_state) {
        case TX_IDLE:
            if (p->tx_len == 0U) {
                flush_ticks = pdMS_TO_TICKS(CONFIG_CONSOLE_USB_CDC_TX_FLUSH_TIMEOUT_MS);
                if ((CONFIG_CONSOLE_USB_CDC_TX_FLUSH_TIMEOUT_MS != 0U) && (flush_ticks == 0U)) {
                    flush_ticks = 1U;
                }
                flags = bflb_irq_save();
                pending_len = Ring_Buffer_Get_Length(&p->tx_ring);
                if ((pending_len != 0U) && (pending_len < 512U) && (flush_ticks != 0U) && !flush_timeout) {
                    bflb_irq_restore(flags);
                    *flush_waiting = true;
                    if (flush_ticks < *wait_ticks)
                        *wait_ticks = flush_ticks;
                    break;
                }
                p->tx_len = pending_len > 512U ? 512U : pending_len;
                if (p->tx_len != 0U) {
                    p->tx_len = Ring_Buffer_Read(&p->tx_ring, p->tx_buffer, p->tx_len);
                }
                bflb_irq_restore(flags);
            }
            if (p->tx_len == 0U) {
                break;
            }
            flags = bflb_irq_save();
            p->tx_done_pending = false;
            bflb_irq_restore(flags);
            if (usbd_ep_start_write(USB_CONSOLE_BUS_ID, p->ep_in, p->tx_buffer, p->tx_len) != 0) {
                return true;
            }
            p->tx_state = TX_WAIT_USBD_IN;
            break;

        case TX_WAIT_USBD_IN:
            flags = bflb_irq_save();
            done = p->tx_done_pending;
            if (done) {
                p->tx_done_pending = false;
            }
            bflb_irq_restore(flags);
            if (!done) {
                break;
            }
            p->tx_len = 0U;
            p->tx_state = TX_IDLE;
            goto polling_continue;

        default:
            p->tx_state = TX_IDLE;
            break;
    }
    return false;
}

/* the SDK's RX state machine, per port: port 0 into the shell, port 1 to the K230 */
static bool rx_polling(struct port *p)
{
    bool done;
    uint32_t rx_len;
    uintptr_t flags;

polling_continue:
    switch (p->rx_state) {
        case RX_START_USBD_OUT:
            flags = bflb_irq_save();
            p->rx_done_pending = false;
            bflb_irq_restore(flags);
            if (usbd_ep_start_read(USB_CONSOLE_BUS_ID, p->ep_out, p->rx_buffer, 512U) != 0) {
                return true;
            }
            p->rx_state = RX_WAIT_USBD_OUT;
            break;

        case RX_WAIT_USBD_OUT:
            flags = bflb_irq_save();
            done = p->rx_done_pending;
            rx_len = p->rx_done_len;
            if (done) {
                p->rx_done_pending = false;
            }
            bflb_irq_restore(flags);
            if (!done) {
                break;
            }
            if ((rx_len != 0U) && (rx_len <= 512U)) {
                if (p->rx)
                    p->rx(p->rx_buffer, rx_len);
                else
                    usb_k230_rx(p->rx_buffer, rx_len);
            }
            p->rx_state = RX_START_USBD_OUT;
            goto polling_continue;

        default:
            p->rx_state = RX_START_USBD_OUT;
            break;
    }
    return false;
}

static void usb_console_task(void *arg)
{
    BaseType_t wait_result;
    bool retry, flush_timeout, flush_waiting;
    TickType_t retry_ticks, wait_ticks;
    uint8_t usb_event;
    uintptr_t flags;

    (void)arg;
    data_reset(USB_CONSOLE_STA_WAIT_USBD_CFG);
    flush_timeout = false;

    for (;;) {
        flags = bflb_irq_save();
        usb_event = usb_console.usb_event;
        usb_console.usb_event = USB_CONSOLE_EVENT_IDLE;
        bflb_irq_restore(flags);

        retry = false;
        flush_waiting = false;
        wait_ticks = portMAX_DELAY;

state_continue:
        switch (usb_console.state) {
            case USB_CONSOLE_STA_WAIT_USBD_CFG:
                if (usb_event == USBD_EVENT_CONFIGURED) {
                    data_reset(USB_CONSOLE_STA_DATA_POLLING);
                    usb_event = USB_CONSOLE_EVENT_IDLE;
                    goto state_continue;
                }
                break;

            case USB_CONSOLE_STA_DATA_POLLING:
                switch (usb_event) {
                    case USBD_EVENT_RESET:
                    case USBD_EVENT_ERROR:
                    case USBD_EVENT_DISCONNECTED:
                    case USBD_EVENT_DEINIT:
                        data_reset(USB_CONSOLE_STA_WAIT_USBD_CFG);
                        break;
                    case USBD_EVENT_CONFIGURED:
                        data_reset(USB_CONSOLE_STA_DATA_POLLING);
                        usb_event = USB_CONSOLE_EVENT_IDLE;
                        goto state_continue;
                    case USBD_EVENT_SUSPEND:
                        data_reset(USB_CONSOLE_STA_SUSPENDED);
                        break;
                    case USB_CONSOLE_EVENT_IDLE:
                        for (unsigned i = 0; i < USB_CONSOLE_PORTS; i++) {
                            if (rx_polling(&ports[i]))
                                retry = true;
                            if (tx_polling(&ports[i], flush_timeout, &wait_ticks, &flush_waiting))
                                retry = true;
                        }
                        flush_timeout = false;
                        break;
                    default:
                        break;
                }
                break;

            case USB_CONSOLE_STA_SUSPENDED:
                switch (usb_event) {
                    case USBD_EVENT_RESET:
                    case USBD_EVENT_ERROR:
                    case USBD_EVENT_DISCONNECTED:
                    case USBD_EVENT_DEINIT:
                        data_reset(USB_CONSOLE_STA_WAIT_USBD_CFG);
                        break;
                    case USBD_EVENT_CONFIGURED:
                        data_reset(USB_CONSOLE_STA_DATA_POLLING);
                        usb_event = USB_CONSOLE_EVENT_IDLE;
                        goto state_continue;
                    case USBD_EVENT_RESUME:
                        usb_console.state = USB_CONSOLE_STA_DATA_POLLING;
                        usb_event = USB_CONSOLE_EVENT_IDLE;
                        goto state_continue;
                    default:
                        break;
                }
                break;

            default:
                data_reset(USB_CONSOLE_STA_WAIT_USBD_CFG);
                break;
        }

        if (retry) {
            retry_ticks = pdMS_TO_TICKS(USB_CONSOLE_RETRY_DELAY_MS);
            if ((wait_ticks == portMAX_DELAY) || (retry_ticks < wait_ticks)) {
                wait_ticks = retry_ticks;
                flush_waiting = false;
            }
        }
        wait_result = xSemaphoreTake(usb_console.event_sem, wait_ticks);
        flush_timeout = flush_waiting && (wait_result == pdFALSE);
    }
}

void usb_console_init(void)
{
    Ring_Buffer_Init(&ports[0].tx_ring, p0_tx_pool, sizeof(p0_tx_pool), NULL, NULL);
    Ring_Buffer_Init(&ports[1].tx_ring, p1_tx_pool, sizeof(p1_tx_pool), NULL, NULL);

    usb_console.event_sem = xSemaphoreCreateBinary();
    if (usb_console.event_sem == NULL)
        vAssertCalled();                 /* no console: no hands-free flash or check (fail fast) */

    shell_init_with_task(NULL);

    usbd_desc_register(USB_CONSOLE_BUS_ID, &usb_console_descriptor);
    for (unsigned i = 0; i < 2 * USB_CONSOLE_PORTS; i++)
        usbd_add_interface(USB_CONSOLE_BUS_ID, usbd_cdc_acm_init_intf(USB_CONSOLE_BUS_ID, &intf[i]));
    for (unsigned i = 0; i < 2 * USB_CONSOLE_PORTS; i++)
        usbd_add_endpoint(USB_CONSOLE_BUS_ID, &endpoints[i]);

#ifdef BOARD_USB_VIA_GPIO
    board_usb_gpio_init();
#endif

    if (usbd_initialize(USB_CONSOLE_BUS_ID, 0, event_cb) != 0 ||
        xTaskCreate(usb_console_task, "usb_console", CONFIG_CONSOLE_USB_CDC_TASK_STACK_SIZE, NULL,
                    configMAX_PRIORITIES - 2U, NULL) != pdPASS)
        vAssertCalled();
}
