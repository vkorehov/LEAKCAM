/*
 * LEAKCAM BL616 Wi-Fi: NetHub message router on top of the SDIO ports.
 *
 * Two channels are used, each with the HOST_READY / DEVICE_START handshake and download
 * credits described in bl616_nethub.h:
 *   netdev (tag 5): Ethernet frames of the BL616 station, both ways
 *   vchan  (tag 13): our control messages (wifi_ctrl_proto.h) as USER virtual-channel data
 * The BL616's keepalive watchdog is left unarmed on purpose: it only starts after the first
 * keepalive the host sends, and a K230 restart is already seen by the BL616 as the SDIO reset
 * of card detection. So nothing here runs periodically except a 1 s safety poll.
 *
 * One worker thread does all SDIO traffic. It wakes on the card interrupt or on a queued
 * transmit. Uploads are drained first, because they carry the credits that unblock
 * downloads and because an upload port stays busy on the BL616 until we read it.
 *
 * Transmit queues are fixed rings of pre-sized slots, so the data path never allocates:
 * lwIP's tcpip thread copies a frame into a slot (the driver advertises copy-before-return,
 * so lwIP hands pbufs over directly) and the worker sends it when a port and a credit are
 * free.
 */
#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "bl616_nethub.h"

#define DBG_TAG "bl616.msg"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define DATA_SLOTS       8
#define DATA_SLOT_SIZE   1536   /* 8-byte header + Ethernet frame up to 1514, as 3 blocks */
#define CTRL_SLOTS       4
#define CTRL_SLOT_SIZE   128    /* 12 bytes of headers + the largest control message (100) */
#define RX_BURST         16     /* uploads per wake-up before the transmit side gets a turn */
#define START_TIMEOUT_MS 1000   /* resend HOST_READY when DEVICE_START does not come */
#define SAFETY_POLL_MS   1000   /* a missed card interrupt costs at most this much latency */

#define WORKER_STACK     8192
#define WORKER_PRIORITY  14     /* just above the WLAN work queue (15), below the SDIO IRQ thread */

#define EV_WAKE          (1u << 0)
#define EV_STOP          (1u << 1)

enum ch_state
{
    CH_RESET,       /* HOST_READY must be sent */
    CH_WAIT,        /* HOST_READY sent, waiting for DEVICE_START */
    CH_RUN,
};

struct ring
{
    rt_uint8_t *buf;
    rt_uint16_t slot_size;
    rt_uint8_t slots;
    rt_uint8_t head;        /* oldest queued slot, advanced by the worker only */
    rt_uint8_t count;       /* head + count = next free slot, grown by producers only */
    rt_uint16_t len[DATA_SLOTS];
    struct rt_mutex lock;   /* producers against each other; count itself is IRQ-locked */
};

struct chan
{
    const char *name;
    rt_uint8_t tag;
    rt_uint8_t state;
    rt_uint8_t credit;      /* BL616's credit_limit */
    rt_uint8_t sent;        /* DNLD_DATA messages sent since DEVICE_START (mod 256) */
    rt_tick_t ready_at;
    struct ring ring;
};

static rt_uint8_t data_buf[DATA_SLOTS * DATA_SLOT_SIZE] __attribute__((aligned(64)));
static rt_uint8_t ctrl_buf[CTRL_SLOTS * CTRL_SLOT_SIZE] __attribute__((aligned(64)));
static rt_uint8_t rx_buf[BL616_UPLD_MAX] __attribute__((aligned(64)));
static rt_uint8_t ready_msg[sizeof(struct bl616_ch_hdr)] __attribute__((aligned(64)));

static struct chan netdev = { .name = "netdev", .tag = BL616_TAG_NETDEV_STA };
static struct chan vchan = { .name = "vchan", .tag = BL616_TAG_VCHAN };

static struct rt_event link_event;
static rt_thread_t worker;
static struct rt_completion worker_done;
static volatile rt_bool_t running;

static void ring_init(struct ring *r, rt_uint8_t *buf, rt_uint16_t slot_size, rt_uint8_t slots,
                      const char *name)
{
    r->buf = buf;
    r->slot_size = slot_size;
    r->slots = slots;
    r->head = 0;
    r->count = 0;
    rt_mutex_init(&r->lock, name, RT_IPC_FLAG_PRIO);
}

/* reserve the next free slot; the caller fills it and commits with ring_push() */
static rt_uint8_t *ring_tail(struct ring *r)
{
    rt_base_t level;
    rt_uint8_t tail = 0;
    rt_bool_t full;

    level = rt_hw_interrupt_disable();
    full = r->count >= r->slots;
    if (!full)
        tail = (r->head + r->count) % r->slots;
    rt_hw_interrupt_enable(level);
    return full ? RT_NULL : r->buf + (rt_size_t)tail * r->slot_size;
}

static void ring_push(struct ring *r, rt_uint8_t *slot, rt_uint16_t len)
{
    rt_base_t level;

    r->len[(slot - r->buf) / r->slot_size] = len;
    level = rt_hw_interrupt_disable();
    r->count++;
    rt_hw_interrupt_enable(level);
}

static void ring_pop(struct ring *r)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    r->head = (r->head + 1) % r->slots;
    r->count--;
    rt_hw_interrupt_enable(level);
}

static void ch_hdr_fill(struct bl616_ch_hdr *h, rt_uint8_t tag, rt_uint8_t flag,
                        rt_size_t payload)
{
    rt_memset(h, 0, sizeof(*h));
    h->mr.tag = tag;
    h->mr.len = (rt_uint16_t)(sizeof(*h) - sizeof(h->mr) + payload);
    h->flag = flag;
}

void bl616_link_wake(void)
{
    /* a card interrupt before the worker runs is harmless: its first pass reads the ports */
    if (running)
        rt_event_send(&link_event, EV_WAKE);
}

static rt_err_t queue(struct chan *ch, const void *a, rt_size_t alen, const void *b,
                      rt_size_t blen)
{
    rt_size_t len = sizeof(struct bl616_ch_hdr) + alen + blen;
    rt_uint8_t *slot;

    if (!running)
        return -RT_EIO;
    if (len > ch->ring.slot_size)
        return -RT_EINVAL;
    rt_mutex_take(&ch->ring.lock, RT_WAITING_FOREVER);
    slot = ring_tail(&ch->ring);
    if (!slot)
    {
        rt_mutex_release(&ch->ring.lock);
        return -RT_EFULL;
    }
    ch_hdr_fill((struct bl616_ch_hdr *)slot, ch->tag, BL616_FLAG_DNLD_DATA, alen + blen);
    rt_memcpy(slot + sizeof(struct bl616_ch_hdr), a, alen);
    if (blen)
        rt_memcpy(slot + sizeof(struct bl616_ch_hdr) + alen, b, blen);
    ring_push(&ch->ring, slot, (rt_uint16_t)len);
    rt_mutex_release(&ch->ring.lock);
    bl616_link_wake();
    return RT_EOK;
}

rt_err_t bl616_link_send_frame(const void *frame, rt_size_t len)
{
    return queue(&netdev, frame, len, RT_NULL, 0);
}

rt_err_t bl616_link_send_ctrl(const void *msg, rt_size_t len)
{
    struct bl616_vchan_hdr vh = { BL616_VCHAN_USER, 0, (rt_uint16_t)len };

    return queue(&vchan, &vh, sizeof(vh), msg, len);
}

/* ---------------------------------------------------------------- receive side */

static void ch_reset(struct chan *ch, const char *why)
{
    if (ch->state != CH_RESET)
        LOG_W("%s: %s, restarting handshake", ch->name, why);
    ch->state = CH_RESET;
}

static void vchan_data(const rt_uint8_t *p, rt_size_t n)
{
    const struct bl616_vchan_hdr *vh = (const struct bl616_vchan_hdr *)p;

    if (n < sizeof(*vh) || vh->len > n - sizeof(*vh))
        return;
    if (vh->type == BL616_VCHAN_USER)
        bl616_wifi_ctrl_rx(p + sizeof(*vh), vh->len);
}

static void dispatch(rt_uint8_t *msg, rt_size_t len)
{
    struct bl616_ch_hdr *h = (struct bl616_ch_hdr *)msg;
    struct chan *ch;
    rt_uint8_t *p = msg + sizeof(*h);
    rt_size_t n;

    /* the upload is padded to whole blocks; the router header has the true length */
    if (len < sizeof(*h) || h->mr.len < sizeof(*h) - sizeof(h->mr) ||
        h->mr.len > len - sizeof(h->mr))
        return;
    n = h->mr.len - (sizeof(*h) - sizeof(h->mr));

    if (h->mr.tag == BL616_TAG_NETDEV_STA)
        ch = &netdev;
    else if (h->mr.tag == BL616_TAG_VCHAN)
        ch = &vchan;
    else
        return;

    /* a credit value is valid from DEVICE_START on; anything the BL616 had queued before
     * our HOST_READY belongs to the previous session */
    if (h->flag == BL616_FLAG_DEVICE_START)
    {
        ch->state = CH_RUN;
        ch->sent = 0;
        ch->credit = h->credit_update ? h->credit_limit : 0;
        LOG_I("%s: started, credit %u", ch->name, ch->credit);
        if (ch == &netdev && n >= 6)
            bl616_wifi_attach(p);
        else if (ch == &vchan)
            bl616_wifi_ctrl_up();
        return;
    }
    if (ch->state != CH_RUN)
        return;
    if (h->credit_update)
        ch->credit = h->credit_limit;

    switch (h->flag)
    {
    case BL616_FLAG_UPLD_DATA:
        if (ch == &netdev)
            bl616_wifi_rx(p, n);
        else
            vchan_data(p, n);
        break;
    case BL616_FLAG_DEVICE_RESET:
        ch_reset(ch, "BL616 reset the channel");
        break;
    case BL616_FLAG_DEVICE_STOP:
        ch_reset(ch, "BL616 stopped the channel");
        break;
    case BL616_FLAG_MAC_IP_UPDATE:
        /* the netif MAC is fixed at registration; the BL616 station MAC does not change */
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- transmit side */

/* returns -RT_EBUSY when a download port was not free, so the worker polls again soon */
static rt_err_t ch_service(struct chan *ch)
{
    struct ring *r = &ch->ring;
    rt_err_t err;

    if (ch->state == CH_WAIT &&
        rt_tick_get() - ch->ready_at > rt_tick_from_millisecond(START_TIMEOUT_MS))
        ch->state = CH_RESET;
    if (ch->state == CH_RESET)
    {
        ch_hdr_fill((struct bl616_ch_hdr *)ready_msg, ch->tag, BL616_FLAG_HOST_READY, 0);
        err = bl616_sdio_write(ready_msg, sizeof(ready_msg));
        if (err != RT_EOK)
            return err;
        ch->state = CH_WAIT;
        ch->ready_at = rt_tick_get();
        return RT_EOK;
    }
    if (ch->state != CH_RUN)
        return RT_EOK;

    while (r->count && (rt_int8_t)(ch->credit - ch->sent) > 0)
    {
        err = bl616_sdio_write(r->buf + (rt_size_t)r->head * r->slot_size, r->len[r->head]);
        if (err == -RT_EBUSY)
            return err;
        if (err != RT_EOK)
            LOG_W("%s: download failed (%d), message dropped", ch->name, err);
        else
            ch->sent++;
        ring_pop(r);
    }
    return RT_EOK;
}

static void worker_entry(void *param)
{
    rt_int32_t timeout = rt_tick_from_millisecond(SAFETY_POLL_MS);
    rt_uint32_t ev;
    rt_size_t len;
    rt_err_t err;
    int i;

    (void)param;
    while (running)
    {
        rt_event_recv(&link_event, EV_WAKE | EV_STOP, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      timeout, &ev);
        if (!running)
            break;

        for (i = 0; i < RX_BURST; i++)
        {
            err = bl616_sdio_read(rx_buf, sizeof(rx_buf), &len);
            if (err == -RT_EEMPTY)
                break;
            if (err != RT_EOK)
            {
                LOG_E("upload failed: %d", err);
                break;
            }
            dispatch(rx_buf, len);
        }

        /* control first: a JOIN must not wait behind a full frame queue */
        err = ch_service(&vchan);
        if (err != -RT_EBUSY)
            err = ch_service(&netdev);
        if (err == -RT_EBUSY || i == RX_BURST)
            timeout = 1;    /* a port or more uploads are pending: look again next tick */
        else
            timeout = rt_tick_from_millisecond(SAFETY_POLL_MS);
    }
    rt_completion_done(&worker_done);
}

rt_err_t bl616_link_start(void)
{
    static rt_bool_t once;

    if (!once)
    {
        rt_event_init(&link_event, "bl616", RT_IPC_FLAG_PRIO);
        ring_init(&netdev.ring, data_buf, DATA_SLOT_SIZE, DATA_SLOTS, "bl616d");
        ring_init(&vchan.ring, ctrl_buf, CTRL_SLOT_SIZE, CTRL_SLOTS, "bl616c");
        once = RT_TRUE;
    }
    if (bl616_sdio_dnld_max() < DATA_SLOT_SIZE)
        LOG_W("BL616 download buffer %u B < %u B: full-size frames will be dropped",
              (unsigned)bl616_sdio_dnld_max(), DATA_SLOT_SIZE);

    netdev.state = vchan.state = CH_RESET;
    netdev.ring.head = netdev.ring.count = 0;
    vchan.ring.head = vchan.ring.count = 0;
    rt_completion_init(&worker_done);
    running = RT_TRUE;
    worker = rt_thread_create("bl616", worker_entry, RT_NULL, WORKER_STACK, WORKER_PRIORITY, 10);
    if (!worker)
    {
        running = RT_FALSE;
        return -RT_ENOMEM;
    }
    rt_thread_startup(worker);
    return RT_EOK;
}

void bl616_link_stop(void)
{
    if (!running)
        return;
    running = RT_FALSE;
    rt_event_send(&link_event, EV_STOP);
    rt_completion_wait(&worker_done, RT_WAITING_FOREVER);
    worker = RT_NULL;
}
