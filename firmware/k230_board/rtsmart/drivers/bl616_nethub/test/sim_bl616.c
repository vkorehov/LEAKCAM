/*
 * Simulated BL616 SDU behind the RT-Thread SDIO core API, plus the WLAN manager calls of
 * bl616_wifi.c. The SDU side follows bl616_nethub.h / bl616_wifi/README.md: registers read by
 * CMD52, 4 upload and 4 download ports used in ring order from OUT_PTR, packed RD_LEN, CMD53 in
 * byte mode up to 512 bytes and whole 512-byte blocks above. Every rule the host must keep is
 * checked here and counted in sim.violations.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <wlan_mgnt.h>

#include "host.h"

struct sim sim;
struct rt_mmcsd_host sim_host;
struct rt_mmcsd_card sim_card;
struct rt_sdio_function sim_func;
struct rt_sdio_driver *sim_driver;
struct sim_wlan wl;
static rt_sdio_irq_handler_t *irq_handler;

static void violation(const char *fmt, ...)
{
    va_list ap;

    sim.violations++;
    printf("  SDU: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void sim_reset(rt_uint8_t out_ptr)
{
    memset(&sim, 0, sizeof(sim));
    sim.card_ready = 1536 / 64;
    sim.status = BL616_STATUS_APP_RUN | BL616_STATUS_RD_LEN_PACKED;
    sim.io_port = 0x012340;
    sim.out_ptr = out_ptr;
    sim.dn_next = out_ptr >> 4;
    sim.up_next = sim.host_up_next = out_ptr & 0x0f;
    sim.auto_arm = 1;
    sim.wr_bitmap = (1u << SIM_PORTS) - 1;   /* the BL616 has handed every buffer over */

    memset(&sim_card, 0, sizeof(sim_card));
    memset(&sim_func, 0, sizeof(sim_func));
    sim_host.lock_depth = 0;
    sim_card.host = &sim_host;
    sim_card.sdio_function_num = 1;
    sim_card.sdio_function[1] = &sim_func;
    sim_func.card = &sim_card;
    sim_func.num = 1;
    sim_func.manufacturer = BL616_SDIO_VENDOR;
    sim_func.product = BL616_SDIO_DEVICE;
}

static void need_lock(const char *what)
{
    if (sim_host.lock_depth <= 0)
        violation("%s without the MMC host lock", what);
    if (!sim.enabled)
        violation("%s on a disabled function", what);
}

/* ---------------------------------------------------------------- SDIO core */

void mmcsd_host_lock(struct rt_mmcsd_host *host) { host->lock_depth++; }

void mmcsd_host_unlock(struct rt_mmcsd_host *host)
{
    if (--host->lock_depth < 0)
        violation("host unlocked more often than locked");
}

rt_int32_t sdio_register_driver(struct rt_sdio_driver *driver)
{
    sim_driver = driver;
    return -RT_EEMPTY;    /* registered, no card yet: the core's answer before card detection */
}

rt_int32_t sdio_enable_func(struct rt_sdio_function *func)
{
    (void)func;
    sim.enabled = 1;
    return RT_EOK;
}

rt_int32_t sdio_disable_func(struct rt_sdio_function *func)
{
    (void)func;
    sim.enabled = 0;
    return RT_EOK;
}

rt_int32_t sdio_set_block_size(struct rt_sdio_function *func, rt_uint32_t blksize)
{
    func->cur_blk_size = blksize;
    sim.block_size = (int)blksize;
    return RT_EOK;
}

rt_int32_t sdio_attach_irq(struct rt_sdio_function *func, rt_sdio_irq_handler_t *handler)
{
    func->irq_handler = handler;
    irq_handler = handler;
    sim.irq_attached = 1;
    return RT_EOK;
}

rt_int32_t sdio_detach_irq(struct rt_sdio_function *func)
{
    func->irq_handler = RT_NULL;
    irq_handler = RT_NULL;
    sim.irq_attached = 0;
    return RT_EOK;
}

/* the core's SDIO interrupt thread calls the handler with the host lock held */
void sim_irq(void)
{
    if (!irq_handler)
        return;
    mmcsd_host_lock(&sim_host);
    irq_handler(&sim_func);
    mmcsd_host_unlock(&sim_host);
}

/* ---------------------------------------------------------------- CMD52 */

rt_uint8_t sdio_io_readb(struct rt_sdio_function *func, rt_uint32_t reg, rt_int32_t *err)
{
    (void)func;
    need_lock("CMD52 read");
    *err = RT_EOK;
    if (reg >= BL616_REG_IO_PORT && reg < BL616_REG_IO_PORT + 3)
        return (rt_uint8_t)(sim.io_port >> (8 * (reg - BL616_REG_IO_PORT)));
    if (reg >= (rt_uint32_t)BL616_REG_RD_LEN(0) && reg < (rt_uint32_t)BL616_REG_RD_LEN(SIM_PORTS))
    {
        unsigned p = (reg - BL616_REG_RD_LEN(0)) / 2, hi = (reg - BL616_REG_RD_LEN(0)) & 1;
        sim.rd_len_hi_reads += hi;
        return sim.rd_len[p][hi];
    }
    switch (reg)
    {
    case BL616_REG_CARD_READY:
        return host_tick >= sim.card_ready_at ? sim.card_ready : 0;
    case BL616_REG_STATUS:
        return sim.status;
    case BL616_REG_OUT_PTR:
        return sim.out_ptr;
    case BL616_REG_RD_BITMAP:
        return sim.rd_bitmap;
    case BL616_REG_RD_BITMAP + 1:
        return 0;
    case BL616_REG_WR_BITMAP:
        return sim.wr_bitmap;
    case BL616_REG_WR_BITMAP + 1:
        return 0;
    default:
        violation("CMD52 read of unknown register 0x%02x", (unsigned)reg);
        *err = -RT_EIO;
        return 0xff;
    }
}

rt_int32_t sdio_io_writeb(struct rt_sdio_function *func, rt_uint32_t reg, rt_uint8_t data)
{
    (void)func;
    need_lock("CMD52 write");
    switch (reg)
    {
    case BL616_REG_HOST_READY:
        sim.host_ready = data;
        return RT_EOK;
    case BL616_REG_HOST_INT_MASK:
        sim.int_mask = data;
        return RT_EOK;
    case BL616_REG_HOST_INT_STATUS:
        if (data != 0)
            violation("HOST_INT_STATUS written with 0x%02x", data);
        sim.int_acks++;
        return RT_EOK;
    default:
        violation("CMD52 write of register 0x%02x", (unsigned)reg);
        return -RT_EIO;
    }
}

/* ---------------------------------------------------------------- CMD53 */

static int cmd53_size_ok(rt_uint32_t len)
{
    return len > 0 && (len <= BL616_BLOCK_SIZE || len % BL616_BLOCK_SIZE == 0);
}

rt_int32_t sdio_io_read_multi_fifo_b(struct rt_sdio_function *func, rt_uint32_t addr,
                                     rt_uint8_t *buf, rt_uint32_t len)
{
    unsigned port = addr - sim.io_port;

    (void)func;
    need_lock("upload read");
    if (addr < sim.io_port || port >= SIM_PORTS)
    {
        violation("upload read from 0x%05x, not a port", (unsigned)addr);
        return -RT_EIO;
    }
    if (!(sim.rd_bitmap & (1u << port)))
        violation("upload read from empty port %u", port);
    if (port != sim.host_up_next % SIM_PORTS)
        violation("upload read from port %u, next in ring order is %u", port,
                  sim.host_up_next % SIM_PORTS);
    if (!cmd53_size_ok(len))
        violation("upload read of %u bytes: neither byte mode nor whole blocks", (unsigned)len);
    if (len != sim.up_xfer[port])
        violation("upload read of %u bytes from port %u, RD_LEN says %u", (unsigned)len, port,
                  (unsigned)sim.up_xfer[port]);
    memcpy(buf, sim.up[port], len < sizeof(sim.up[port]) ? len : sizeof(sim.up[port]));
    sim.rd_bitmap &= ~(1u << port);
    sim.host_up_next++;
    sim.up_reads++;
    return RT_EOK;
}

rt_int32_t sdio_io_write_multi_fifo_b(struct rt_sdio_function *func, rt_uint32_t addr,
                                      rt_uint8_t *buf, rt_uint32_t len)
{
    unsigned port = addr - sim.io_port;
    struct sim_msg *m;
    rt_size_t msg_len, want;

    (void)func;
    need_lock("download write");
    if (addr < sim.io_port || port >= SIM_PORTS)
    {
        violation("download write to 0x%05x, not a port", (unsigned)addr);
        return -RT_EIO;
    }
    if (!(sim.wr_bitmap & (1u << port)))
        violation("download write to port %u, which has no buffer", port);
    if (port != sim.dn_next % SIM_PORTS)
        violation("download write to port %u, next in ring order is %u", port,
                  sim.dn_next % SIM_PORTS);
    if (!cmd53_size_ok(len))
        violation("download write of %u bytes: neither byte mode nor whole blocks", (unsigned)len);
    if (len > (rt_uint32_t)sim.card_ready * BL616_SIZE_UNIT)
        violation("download of %u bytes, BL616 buffer is %u", (unsigned)len,
                  (unsigned)sim.card_ready * BL616_SIZE_UNIT);
    /* the router header holds the true length; the transfer is that, padded as the SDU wants */
    msg_len = 4 + (rt_size_t)(buf[2] | buf[3] << 8);
    want = msg_len <= BL616_BLOCK_SIZE ? msg_len : RT_ALIGN(msg_len, BL616_BLOCK_SIZE);
    if (len != want)
        violation("download of a %u-byte message as %u bytes, expected %u", (unsigned)msg_len,
                  (unsigned)len, (unsigned)want);
    if (sim.ndn >= SIM_LOG)
        violation("download log full, make SIM_LOG larger");
    else
    {
        m = &sim.dn[sim.ndn++];
        m->len = msg_len < sizeof(m->buf) ? msg_len : sizeof(m->buf);
        m->xfer = len;
        m->port = port;
        memcpy(m->buf, buf, m->len);
    }
    sim.wr_bitmap &= ~(1u << port);
    if (sim.auto_arm)
        sim.wr_bitmap |= 1u << port;
    sim.dn_next++;
    return RT_EOK;
}

/* ---------------------------------------------------------------- BL616 side */

static int upload_port(const void *data, rt_size_t len, rt_size_t xfer, rt_uint16_t raw,
                       int two_bytes)
{
    unsigned port = sim.up_next % SIM_PORTS;

    if (sim.rd_bitmap & (1u << port) || xfer > sizeof(sim.up[port]))
        return -1;
    memset(sim.up[port], 0xee, sizeof(sim.up[port]));   /* padding is not zero on the wire */
    if (len)
        memcpy(sim.up[port], data, len);
    sim.up_xfer[port] = xfer;
    sim.rd_len[port][0] = (rt_uint8_t)(raw | (two_bytes ? 1 : 0));
    /* the high byte holds stale data unless bit 0 says it counts */
    sim.rd_len[port][1] = two_bytes ? (rt_uint8_t)(raw >> 8) : 0xa5;
    sim.rd_bitmap |= 1u << port;
    sim.up_next++;
    return 0;
}

/* the SDU sizes an upload in bytes up to one block, in blocks above; one byte when it fits */
int sim_upload(const void *msg, rt_size_t len)
{
    int blocks = len > BL616_BLOCK_SIZE;
    rt_size_t count = blocks ? (len + BL616_BLOCK_SIZE - 1) / BL616_BLOCK_SIZE : len;
    rt_uint16_t raw = (rt_uint16_t)(count << 2 | (blocks ? 2 : 0));

    return upload_port(msg, len, blocks ? count * BL616_BLOCK_SIZE : len, raw, raw > 0xff);
}

int sim_upload_raw(rt_uint16_t rd_len, int two_bytes, const void *data, rt_size_t xfer)
{
    return upload_port(data, xfer < 4096 ? xfer : 0, xfer, rd_len, two_bytes);
}

rt_size_t sim_ch_msg(rt_uint8_t *out, rt_uint8_t tag, rt_uint8_t flag, rt_uint8_t cu,
                     rt_uint8_t cl, const void *payload, rt_size_t n)
{
    rt_size_t body = 4 + n;

    out[0] = tag;
    out[1] = 0;
    out[2] = (rt_uint8_t)body;
    out[3] = (rt_uint8_t)(body >> 8);
    out[4] = 0;
    out[5] = flag;
    out[6] = cu;
    out[7] = cl;
    if (n)
        memcpy(out + 8, payload, n);
    return 8 + n;
}

/* ---------------------------------------------------------------- WLAN manager */

rt_err_t rt_wlan_dev_register_auto(struct rt_wlan_device *wlan, const char *model_name,
                                   rt_wlan_mode_t mode, rt_wlan_transport_t transport,
                                   const struct rt_wlan_dev_ops *ops, void *user_data)
{
    (void)model_name;
    (void)mode;
    if (transport != RT_WLAN_TRANSPORT_SDIO)
        violation("WLAN device registered with transport %d", transport);
    if (wl.register_result != RT_EOK)
        return wl.register_result;
    wl.dev = wlan;
    wl.ops = ops;
    wl.registered = 1;
    wl.registrations++;
    wlan->ops = ops;
    wlan->user_data = user_data;
    strcpy(wlan->device.parent.name, "wlan0");
    return RT_EOK;
}

rt_err_t rt_wlan_dev_unregister(struct rt_wlan_device *wlan)
{
    (void)wlan;
    wl.registered = 0;
    return RT_EOK;
}

rt_err_t rt_wlan_set_mode(const char *dev_name, rt_wlan_mode_t mode)
{
    if (!wl.dev || strcmp(dev_name, wl.dev->device.parent.name))
        violation("set_mode on unknown device %s", dev_name);
    wl.mode = mode;
    /* the manager passes the mode on to the driver */
    return wl.ops->wlan_mode(wl.dev, mode);
}

void rt_wlan_dev_indicate_event_handle(struct rt_wlan_device *device, rt_wlan_dev_event_t event,
                                       struct rt_wlan_buff *buff)
{
    if (device != wl.dev || !wl.registered)
        violation("WLAN event %d on an unregistered device", event);
    if (wl.nev >= SIM_EVENTS)
        return;
    if (event == RT_WLAN_DEV_EVT_SCAN_REPORT)
    {
        if (!buff || buff->len != (rt_int32_t)sizeof(struct rt_wlan_info))
            violation("scan report without a struct rt_wlan_info");
        else
            wl.scan[wl.nev] = *(struct rt_wlan_info *)buff->data;
    }
    wl.ev[wl.nev++] = event;
}

rt_err_t rt_wlan_dev_report_data(struct rt_wlan_device *device, void *buff, int len)
{
    if (device != wl.dev || !wl.registered)
        violation("frame reported on an unregistered device");
    if (wl.nrx < SIM_LOG && len > 0 && len <= (int)sizeof(wl.rx[0]))
    {
        memcpy(wl.rx[wl.nrx], buff, (size_t)len);
        wl.rx_len[wl.nrx++] = (rt_size_t)len;
    }
    return RT_EOK;
}
