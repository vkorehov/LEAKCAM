/*
 * LEAKCAM link mux: the K230 console and the BL616 link share UART1 (IO40/IO41 <-> BL616 UART0, LINK_BAUD).
 *
 * The CH340 debug console (UART0, USB-C 2) lives on the K230's switched 3V3 and re-enumerates with every power
 * cycle, so the console moved to the wire the BL616 already listens on (2026-10-22, user: one USB device, fewest
 * clicks). The BL616 splits what we send into link frames and console text and shows the text on its second USB
 * port (/dev/ttyACM1); here the same splitter (k230_linesplit.h, shared with the BL616) splits what it sends:
 *
 *   uart1 (owned here) --rx--> k230_linesplit --> valid frames '$...*XX\n'  -> /dev/link  (leakcam_agent)
 *                                            \--> everything else          -> "kcon"     (console: msh, stdin)
 *   kprintf, stdout, msh (console "kcon") --> uart1 <-- /dev/link writes (one frame per write)
 *
 * "kcon" becomes the console's I/O device (rt_console_set_device), so the tty console, msh and every program's
 * stdin/stdout use it unchanged. Until this driver starts, the console is uart1 itself (CONFIG_RTT_CONSOLE_ID 1),
 * so the earliest kernel lines go out the same wire. A console line and a frame written at the same moment may mix
 * on the wire: the frame then fails its CRC and the link resends it (LINK_ACK_TIMEOUT_MS); nothing is lost.
 * link_mux_pause() hands uart1 to the raw UART bench (uart_bench, tests/test018) for its run.
 */
#include <stdlib.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <string.h>

#ifdef RT_USING_POSIX
#include <dfs_posix.h>
#include <dfs_poll.h>
#endif

#include "k230_linesplit.h"

#define LINK_MUX_UART   "uart1"
#define KCON_RING_SIZE  2048
#define LINK_RING_SIZE  1024

static rt_device_t uart;
static struct linesplit split;
static volatile int paused;
/* tests/test039 (2026-10-27): where a typed console command gets lost - bytes from uart1, console text bytes, text
 * bytes the console ring had no room for, frames taken, frames dropped (link ring full), reads while paused */
static volatile rt_uint32_t st_rx, st_text, st_text_drop, st_frames, st_frame_drop, st_paused;

/* ---------------- the console's I/O device ---------------- */

static struct rt_device kcon;
static struct rt_ringbuffer kcon_rb;
static rt_uint8_t kcon_pool[KCON_RING_SIZE];
static struct rt_device_notify kcon_notify;    /* the tty console's receive callback (RT_DEVICE_CTRL_NOTIFY_SET) */
static rt_uint16_t kcon_oflag;

static rt_err_t kcon_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    kcon_oflag = oflag;
    return RT_EOK;
}

static rt_size_t kcon_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    (void)dev;
    (void)pos;
    rt_base_t level = rt_hw_interrupt_disable();
    rt_size_t n = rt_ringbuffer_get(&kcon_rb, buf, (rt_uint16_t)size);
    rt_hw_interrupt_enable(level);
    return n;
}

static rt_size_t kcon_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    (void)dev;
    (void)pos;
    if (!uart)
        return size;
    if (!(kcon_oflag & RT_DEVICE_FLAG_STREAM))
        return rt_device_write(uart, 0, buf, size);
    /* a stream console: "\n" goes out as "\r\n", as the serial driver does for its console */
    const char *p = buf, *run = p;
    for (rt_size_t i = 0; i < size; i++) {
        if (p[i] == '\n') {
            if (&p[i] > run)
                rt_device_write(uart, 0, run, &p[i] - run);
            rt_device_write(uart, 0, "\r\n", 2);
            run = &p[i + 1];
        }
    }
    if (&p[size] > run)
        rt_device_write(uart, 0, run, &p[size] - run);
    return size;
}

static rt_err_t kcon_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev;
    if (cmd == RT_DEVICE_CTRL_NOTIFY_SET && args) {
        kcon_notify = *(struct rt_device_notify *)args;
    } else if (cmd == RT_DEVICE_CTRL_CONSOLE_OFLAG && args) {
        *(rt_uint16_t *)args = RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_STREAM;
    }
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops kcon_ops = { RT_NULL, kcon_open, RT_NULL, kcon_read, kcon_write, kcon_control };
#endif

/* ---------------- /dev/link: the agent's frames ---------------- */

static struct rt_device link;
static struct rt_ringbuffer link_rb;
static rt_uint8_t link_pool[LINK_RING_SIZE];

static rt_size_t link_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    (void)dev;
    (void)pos;
    rt_base_t level = rt_hw_interrupt_disable();
    rt_size_t n = rt_ringbuffer_get(&link_rb, buf, (rt_uint16_t)size);
    rt_hw_interrupt_enable(level);
    return n;
}

static rt_size_t link_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    (void)dev;
    (void)pos;
    return uart ? rt_device_write(uart, 0, buf, size) : 0;   /* one frame per write: send_frame() */
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops link_ops = { RT_NULL, RT_NULL, RT_NULL, link_read, link_write, RT_NULL };
#endif

#ifdef RT_USING_POSIX
/* as the serial driver's fops (serial.c): non-blocking reads return 0, poll() reports POLLIN */
static int link_fops_open(struct dfs_fd *fd)
{
    (void)fd;
    return 0;
}

static int link_fops_close(struct dfs_fd *fd)
{
    (void)fd;
    return 0;
}

static int link_fops_read(struct dfs_fd *fd, void *buf, size_t count)
{
    int size;
    while ((size = (int)link_read(&link, 0, buf, count)) <= 0) {
        if (fd->flags & O_NONBLOCK)
            return 0;
        if (rt_wqueue_wait_interruptible(&link.wait_queue, 0, RT_WAITING_FOREVER) != RT_EOK)
            return 0;
    }
    return size;
}

static int link_fops_write(struct dfs_fd *fd, const void *buf, size_t count)
{
    (void)fd;
    return (int)link_write(&link, 0, buf, count);
}

static int link_fops_poll(struct dfs_fd *fd, struct rt_pollreq *req)
{
    int mask = 0;
    rt_poll_add(&link.wait_queue, req);
    rt_base_t level = rt_hw_interrupt_disable();
    if (rt_ringbuffer_data_len(&link_rb))
        mask |= POLLIN;
    rt_hw_interrupt_enable(level);
    (void)fd;
    return mask;
}

static const struct dfs_file_ops link_fops = {
    link_fops_open, link_fops_close, RT_NULL, link_fops_read, link_fops_write, RT_NULL, RT_NULL, RT_NULL,
    link_fops_poll,
};
#endif

/* ---------------- the split, in uart1's receive interrupt ---------------- */

static void on_text(void *ctx, const char *p, unsigned n)
{
    (void)ctx;
    rt_size_t put = rt_ringbuffer_put(&kcon_rb, (const rt_uint8_t *)p, (rt_uint16_t)n);
    st_text += n;
    st_text_drop += n - put;
}

static void on_frame(void *ctx, const char *line, unsigned n)
{
    (void)ctx;
    /* whole frames only: a frame that does not fit is dropped (the sender resends it) */
    if (rt_ringbuffer_space_len(&link_rb) >= n + 1) {
        rt_ringbuffer_put(&link_rb, (const rt_uint8_t *)line, (rt_uint16_t)n);
        rt_ringbuffer_putchar(&link_rb, '\n');
        st_frames++;
    } else {
        st_frame_drop++;
    }
}

static rt_err_t uart_rx_ind(rt_device_t dev, rt_size_t size)
{
    (void)size;
    char buf[64];
    rt_size_t n;
    if (paused) {
        st_paused++;
        return RT_EOK;
    }
    rt_size_t text_before = rt_ringbuffer_data_len(&kcon_rb), link_before = rt_ringbuffer_data_len(&link_rb);
    while ((n = rt_device_read(dev, 0, buf, sizeof(buf))) > 0) {
        st_rx += n;
        linesplit_feed(&split, buf, (unsigned)n);
    }
    if (rt_ringbuffer_data_len(&kcon_rb) != text_before && kcon_notify.notify)
        kcon_notify.notify(kcon_notify.dev);
    if (rt_ringbuffer_data_len(&link_rb) != link_before)
        rt_wqueue_wakeup(&link.wait_queue, (void *)POLLIN);
    return RT_EOK;
}

/* tests/test018: the raw UART bench takes uart1 for its run (no split, no console input meanwhile) */
void link_mux_pause(int pause)
{
    paused = pause;
    if (!pause && uart)
        rt_device_set_rx_indicate(uart, uart_rx_ind);
}

static int link_mux_init(void)
{
    rt_ringbuffer_init(&kcon_rb, kcon_pool, sizeof(kcon_pool));
    rt_ringbuffer_init(&link_rb, link_pool, sizeof(link_pool));
    linesplit_init(&split, on_text, on_frame, RT_NULL);

    kcon.type = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    kcon.ops = &kcon_ops;
#else
    kcon.open = kcon_open;
    kcon.read = kcon_read;
    kcon.write = kcon_write;
    kcon.control = kcon_control;
#endif
    link.type = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    link.ops = &link_ops;
#else
    link.read = link_read;
    link.write = link_write;
#endif
    rt_wqueue_init(&link.wait_queue);
#ifdef RT_USING_POSIX
    link.fops = &link_fops;
#endif
    if (rt_device_register(&kcon, "kcon", RT_DEVICE_FLAG_RDWR) != RT_EOK ||
        rt_device_register(&link, "link", RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("link_mux: cannot register kcon / link\n");
        return -1;
    }

    rt_device_t u = rt_device_find(LINK_MUX_UART);
    if (!u) {
        rt_kprintf("link_mux: no %s\n", LINK_MUX_UART);
        return -1;
    }
    /* the console lets go of uart1 first (it clears its notify and closes it), then uart1 is ours */
    uart = u;
    rt_console_set_device("kcon");
    if (rt_device_open(u, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
        rt_kprintf("link_mux: cannot open %s\n", LINK_MUX_UART);
        return -1;
    }
    rt_device_set_rx_indicate(u, uart_rx_ind);
    rt_kprintf("link_mux: console and /dev/link share %s\n", LINK_MUX_UART);
    return 0;
}
INIT_COMPONENT_EXPORT(link_mux_init);

extern volatile rt_uint32_t uart_rx_err[12];     /* drv_uart.c (install.sh: rx error, byte, reset and irq counters) */

static int link_stat(void)
{
    rt_kprintf("link_stat: rx %u text %u text_drop %u frames %u frame_drop %u paused %u kcon_free %u\n",
               (unsigned)st_rx, (unsigned)st_text, (unsigned)st_text_drop, (unsigned)st_frames,
               (unsigned)st_frame_drop, (unsigned)st_paused, (unsigned)rt_ringbuffer_space_len(&kcon_rb));
    rt_kprintf("link_stat: uart1 errors framing %u parity %u overrun %u, bytes dropped %u, last LSR 0x%02x, fifo bytes %u\n",
               (unsigned)uart_rx_err[0], (unsigned)uart_rx_err[1], (unsigned)uart_rx_err[2], (unsigned)uart_rx_err[3],
               (unsigned)uart_rx_err[4], (unsigned)uart_rx_err[5]);
    rt_kprintf("link_stat: uart1 resets %u, irqs %u (rx data %u, char timeout %u, line status %u, other %u)\n",
               (unsigned)uart_rx_err[6], (unsigned)uart_rx_err[7], (unsigned)uart_rx_err[8], (unsigned)uart_rx_err[9],
               (unsigned)uart_rx_err[10], (unsigned)uart_rx_err[11]);
    return 0;
}
MSH_CMD_EXPORT(link_stat, test039: uart1 bytes in / console text / dropped / frames);

/* test039: the link's rate for a run (the BL616 follows with pt_uart baud); the boot console stays at its rate */
static int link_baud(int argc, char **argv)
{
    if (argc < 2 || !uart)
        return -1;
    struct serial_configure cfg = ((struct rt_serial_device *)uart)->config;   /* the device's own, bufsz included */
    cfg.baud_rate = (rt_uint32_t)atoi(argv[1]);
    rt_err_t e = rt_device_control(uart, RT_DEVICE_CTRL_CONFIG, &cfg);
    rt_kprintf("link_baud: %u -> %d\n", (unsigned)cfg.baud_rate, (int)e);
    return 0;
}
MSH_CMD_EXPORT(link_baud, test039: uart1 baud for a test run);
