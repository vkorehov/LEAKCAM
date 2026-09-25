/*
 * LEAKCAM BL616 Wi-Fi: SDIO transport to the BL616 SDU (function 1, 0x424c:0x0606).
 *
 * The SDU moves whole messages through BL616_PORTS ports per direction, used in strict ring
 * order by both ends:
 *   upload (BL616 -> K230): the BL616 queues a message in port n, sets bit n of RD_BITMAP and
 *     pulls DAT1 (card interrupt). We read RD_LEN(n), then read the message from the port with
 *     one CMD53; the SDU clears bit n when the transfer ends.
 *   download (K230 -> BL616): the BL616 hands a free buffer to port n and sets bit n of
 *     WR_BITMAP; we write one message to the port and the SDU clears the bit.
 * Both ends keep their own port counter; at attach the BL616 publishes its current counters
 * in OUT_PTR, so a warm restart of the K230 driver lines up again.
 *
 * Bitmap bits are cached: a bit read as set stays ours until we use that port, so each
 * message costs one bitmap read at most (fewer when several are queued).
 *
 * Every CMD52/CMD53 here runs under the MMC host lock: the core's SDIO interrupt thread
 * talks to the card too, and the K230 SDHCI driver does not serialise requests itself.
 */
#include <rtthread.h>
#include <drivers/mmcsd_core.h>
#include <drivers/sdio.h>

#include "bl616_nethub.h"

#define DBG_TAG "bl616.sdio"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define CARD_READY_TIMEOUT_MS  1000   /* the SDU publishes CARD_READY in bflb_sdio2_init() */

struct bl616_sdio
{
    struct rt_sdio_function *func;
    rt_uint32_t io_port;       /* CMD53 address of port 0 */
    rt_size_t dnld_max;        /* BL616 download buffer, bytes */
    rt_uint8_t dnld_next;      /* next download port (mod BL616_PORTS) */
    rt_uint8_t upld_next;      /* next upload port */
    rt_uint16_t wr_bitmap;     /* cached WR_BITMAP bits not used yet */
    rt_uint16_t rd_bitmap;     /* cached RD_BITMAP bits not read yet */
};

static struct bl616_sdio sd;

static rt_err_t reg_read(rt_uint32_t reg, rt_uint8_t *val)
{
    rt_int32_t err = RT_EOK;

    *val = sdio_io_readb(sd.func, reg, &err);
    return err;
}

static rt_err_t reg_write(rt_uint32_t reg, rt_uint8_t val)
{
    return sdio_io_writeb(sd.func, reg, val);
}

/* short messages go in CMD53 byte mode, longer ones as whole 512-byte blocks: that is how
 * the SDU sizes its uploads, and the BL616 reads the true length from the router header */
static rt_size_t xfer_len(rt_size_t len)
{
    return len <= BL616_BLOCK_SIZE ? len : RT_ALIGN(len, BL616_BLOCK_SIZE);
}

rt_size_t bl616_sdio_dnld_max(void)
{
    return sd.dnld_max;
}

/*
 * RD_LEN(n), packed form (BL616_STATUS_RD_LEN_PACKED): bit 0 set = two bytes, else one;
 * bit 1 set = count of blocks, else of bytes; the count is the value >> 2.
 */
static rt_err_t upld_len(rt_uint8_t port, rt_size_t *len)
{
    rt_uint8_t lo, hi = 0;
    rt_uint16_t raw;
    rt_err_t err;

    err = reg_read(BL616_REG_RD_LEN(port), &lo);
    if (err == RT_EOK && (lo & 0x01))
        err = reg_read(BL616_REG_RD_LEN(port) + 1, &hi);
    if (err != RT_EOK)
        return err;
    raw = (rt_uint16_t)(hi << 8 | lo);
    *len = (raw & 0x02) ? (rt_size_t)(raw >> 2) * BL616_BLOCK_SIZE : (rt_size_t)(raw >> 2);
    return RT_EOK;
}

rt_err_t bl616_sdio_read(rt_uint8_t *buf, rt_size_t size, rt_size_t *len)
{
    struct rt_mmcsd_host *host;
    rt_uint8_t port, bits;
    rt_size_t n = 0;
    rt_err_t err;

    if (!sd.func)
        return -RT_EIO;
    host = sd.func->card->host;
    mmcsd_host_lock(host);

    port = sd.upld_next % BL616_PORTS;
    if (!(sd.rd_bitmap & (1u << port)))
    {
        err = reg_read(BL616_REG_RD_BITMAP, &bits);
        if (err != RT_EOK)
            goto out;
        sd.rd_bitmap |= bits;
        if (!(sd.rd_bitmap & (1u << port)))
        {
            err = -RT_EEMPTY;
            goto out;
        }
    }

    err = upld_len(port, &n);
    if (err != RT_EOK)
        goto out;
    if (n == 0 || xfer_len(n) > size)
    {
        /* the BL616 never sends more than we announced in HOST_READY; if it does, the link
         * is out of step and nothing can be recovered without a reset of both ends */
        LOG_E("upload port %u: bad length %u", port, (unsigned)n);
        err = -RT_EIO;
        goto out;
    }

    err = sdio_io_read_multi_fifo_b(sd.func, sd.io_port + port, buf, xfer_len(n));
    if (err == RT_EOK)
    {
        sd.rd_bitmap &= ~(1u << port);
        sd.upld_next++;
        *len = n;
    }
out:
    mmcsd_host_unlock(host);
    return err;
}

rt_err_t bl616_sdio_write(const rt_uint8_t *buf, rt_size_t len)
{
    struct rt_mmcsd_host *host;
    rt_uint8_t port, bits;
    rt_err_t err;

    if (!sd.func)
        return -RT_EIO;
    if (xfer_len(len) > sd.dnld_max)
        return -RT_EINVAL;
    host = sd.func->card->host;
    mmcsd_host_lock(host);

    port = sd.dnld_next % BL616_PORTS;
    if (!(sd.wr_bitmap & (1u << port)))
    {
        err = reg_read(BL616_REG_WR_BITMAP, &bits);
        if (err != RT_EOK)
            goto out;
        sd.wr_bitmap |= bits;
        if (!(sd.wr_bitmap & (1u << port)))
        {
            err = -RT_EBUSY;    /* the BL616 has not re-armed that buffer yet */
            goto out;
        }
    }

    /* the fifo helper keeps the address fixed, as a port expects; buf holds xfer_len(len) */
    err = sdio_io_write_multi_fifo_b(sd.func, sd.io_port + port, (rt_uint8_t *)buf,
                                     xfer_len(len));
    if (err == RT_EOK)
    {
        sd.wr_bitmap &= ~(1u << port);
        sd.dnld_next++;
    }
out:
    mmcsd_host_unlock(host);
    return err;
}

/* runs in the core's SDIO interrupt thread, which already holds the host lock */
static void bl616_sdio_irq(struct rt_sdio_function *func)
{
    /* release DAT1 before the core re-enables the host interrupt, or it fires again at once */
    sdio_io_writeb(func, BL616_REG_HOST_INT_STATUS, 0);
    bl616_link_wake();
}

/* the SDU handshake: wait for the BL616 application, read its geometry, announce ourselves */
static rt_err_t sdu_attach(void)
{
    rt_tick_t t0 = rt_tick_get();
    rt_uint8_t v, flags, ptr;
    rt_err_t err;
    int i;

    for (;;)
    {
        err = reg_read(BL616_REG_CARD_READY, &v);
        if (err != RT_EOK)
            return err;
        if (v)
            break;
        if (rt_tick_get() - t0 > rt_tick_from_millisecond(CARD_READY_TIMEOUT_MS))
        {
            LOG_E("BL616 SDU never became ready");
            return -RT_ETIMEOUT;
        }
        rt_thread_mdelay(1);
    }
    sd.dnld_max = (rt_size_t)v * BL616_SIZE_UNIT;

    err = reg_read(BL616_REG_STATUS, &flags);
    if (err != RT_EOK)
        return err;
    if (!(flags & (BL616_STATUS_APP_RUN | BL616_STATUS_NOT_SDIO_BOOT)))
    {
        /* the boot ROM answers here when the flash holds no application */
        LOG_E("BL616 is in its SDIO boot ROM (status 0x%02x), no firmware running", flags);
        return -RT_ENOSYS;
    }
    if (!(flags & BL616_STATUS_RD_LEN_PACKED))
    {
        LOG_E("BL616 firmware without packed RD_LEN (status 0x%02x)", flags);
        return -RT_ENOSYS;
    }

    sd.io_port = 0;
    for (i = 0; i < 3; i++)
    {
        err = reg_read(BL616_REG_IO_PORT + i, &v);
        if (err != RT_EOK)
            return err;
        sd.io_port |= (rt_uint32_t)v << (8 * i);
    }

    err = reg_read(BL616_REG_OUT_PTR, &ptr);
    if (err != RT_EOK)
        return err;
    sd.dnld_next = (ptr >> 4) % BL616_PORTS;
    sd.upld_next = (ptr & 0x0f) % BL616_PORTS;
    sd.wr_bitmap = 0;
    sd.rd_bitmap = 0;

    /* HOST_READY starts the BL616's message task; then ask for upload interrupts */
    err = reg_write(BL616_REG_HOST_READY, BL616_UPLD_MAX / BL616_SIZE_UNIT);
    if (err == RT_EOK)
        err = reg_write(BL616_REG_HOST_INT_MASK, 0x01);
    if (err == RT_EOK)
        LOG_I("BL616 SDU ready: io port 0x%05x, download %u B, ports %u/%u",
              (unsigned)sd.io_port, (unsigned)sd.dnld_max, sd.dnld_next, sd.upld_next);
    return err;
}

/* called by the SDIO core from its detect thread, with the host lock held */
static rt_int32_t bl616_sdio_probe(struct rt_mmcsd_card *card)
{
    struct rt_sdio_function *func;
    rt_err_t err;

    if (sd.func || card->sdio_function_num < 1 || !card->sdio_function[1])
        return -RT_EINVAL;
    func = card->sdio_function[1];
    rt_memset(&sd, 0, sizeof(sd));
    sd.func = func;

    /* the worker starts last: it needs the host lock, which this thread holds until the
     * probe returns, so nothing after its start may wait for it */
    err = sdio_enable_func(func);
    if (err == RT_EOK)
        err = sdio_set_block_size(func, BL616_BLOCK_SIZE);
    if (err == RT_EOK)
        err = sdu_attach();
    if (err == RT_EOK)
    {
        err = sdio_attach_irq(func, bl616_sdio_irq);
        if (err == RT_EOK)
        {
            err = bl616_link_start();
            if (err != RT_EOK)
                sdio_detach_irq(func);
        }
    }
    if (err != RT_EOK)
    {
        LOG_E("probe failed: %d", (int)err);
        sdio_disable_func(func);
        sd.func = RT_NULL;
        return err;
    }
    LOG_I("bound SDIO %04x:%04x", func->manufacturer, func->product);
    return RT_EOK;
}

/* the SDIO core does not support removing an SDIO card at run time; this runs only if it
 * ever unbinds us. On LEAKCAM the BL616 cannot go away while the K230 runs: its reset
 * releases K230_PWR, so a BL616 restart is always a K230 power cycle as well. */
static rt_int32_t bl616_sdio_remove(struct rt_mmcsd_card *card)
{
    struct rt_sdio_function *func = sd.func;

    (void)card;
    if (!func)
        return RT_EOK;
    sdio_detach_irq(func);
    bl616_link_stop();
    bl616_wifi_detach();
    sdio_disable_func(func);
    sd.func = RT_NULL;
    return RT_EOK;
}

static struct rt_sdio_device_id bl616_sdio_id = {
    SDIO_ANY_FUNC_ID, BL616_SDIO_VENDOR, BL616_SDIO_DEVICE,
};

static struct rt_sdio_driver bl616_sdio_driver = {
    "bl616-nethub", bl616_sdio_probe, bl616_sdio_remove, &bl616_sdio_id, 0,
};

static int bl616_sdio_init(void)
{
    rt_int32_t err = sdio_register_driver(&bl616_sdio_driver);

    /* -RT_EEMPTY: registered, no card yet; the board's SDIO Wi-Fi thread probes MMC0 later */
    return err == -RT_EEMPTY ? RT_EOK : err;
}
INIT_COMPONENT_EXPORT(bl616_sdio_init);
