/* Host tests for the K230 BL616 NetHub driver (bl616_sdio.c, bl616_msg.c, bl616_wifi.c), built
 * unchanged against test/stub and driven through the simulated SDU of sim_bl616.c. */
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "wifi_ctrl_proto.h"

extern int (*const rt_init_bl616_sdio_init)(void);

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); \
                                      printf("\n"); fails++; } } while (0)

#define NET  BL616_TAG_NETDEV_STA
#define VCH  BL616_TAG_VCHAN

static const rt_uint8_t mac[6] = { 0x02, 0x4c, 0x42, 0x16, 0x16, 0x01 };

/* ---------------------------------------------------------------- helpers */

static rt_err_t probe(void)
{
    rt_err_t err;

    mmcsd_host_lock(&sim_host);    /* the core's detect thread holds it around probe */
    err = sim_driver->probe(&sim_card);
    mmcsd_host_unlock(&sim_host);
    return err;
}

static void unbind(void)
{
    mmcsd_host_lock(&sim_host);
    sim_driver->remove(&sim_card);
    mmcsd_host_unlock(&sim_host);
    CHECK(!host_worker_alive(), "worker still running after remove");
}

static void boot(rt_uint8_t out_ptr)
{
    sim_reset(out_ptr);
    wl.nev = wl.nrx = 0;
    wl.registrations = 0;
    CHECK(probe() == RT_EOK, "probe failed");
}

static void up(rt_uint8_t tag, rt_uint8_t flag, rt_uint8_t cu, rt_uint8_t cl, const void *p,
               rt_size_t n)
{
    static rt_uint8_t m[4096];
    rt_size_t len = sim_ch_msg(m, tag, flag, cu, cl, p, n);

    CHECK(sim_upload(m, len) == 0, "all upload ports busy");
}

static void up_ctrl(const void *msg, rt_size_t n)
{
    rt_uint8_t v[256];

    v[0] = BL616_VCHAN_USER;
    v[1] = 0;
    v[2] = (rt_uint8_t)n;
    v[3] = (rt_uint8_t)(n >> 8);
    memcpy(v + 4, msg, n);
    up(VCH, BL616_FLAG_UPLD_DATA, 0, 0, v, n + 4);
}

static void irq_step(void)
{
    sim_irq();
    host_step();
}

static const struct sim_msg *dn_find(unsigned from, rt_uint8_t tag, rt_uint8_t flag, unsigned nth)
{
    for (unsigned i = from; i < sim.ndn; i++)
        if (sim.dn[i].buf[0] == tag && sim.dn[i].buf[5] == flag && nth-- == 0)
            return &sim.dn[i];
    return NULL;
}

static unsigned dn_count(unsigned from, rt_uint8_t tag, rt_uint8_t flag)
{
    unsigned n = 0;

    while (dn_find(from, tag, flag, n))
        n++;
    return n;
}

/* both channels through HOST_READY / DEVICE_START */
static void start(rt_uint8_t net_credit, rt_uint8_t vch_credit)
{
    rt_uint8_t pl[10] = { 0 };

    memcpy(pl, mac, 6);
    pl[6] = 192, pl[7] = 168, pl[8] = 1, pl[9] = 50;
    host_step();
    up(VCH, BL616_FLAG_DEVICE_START, 1, vch_credit, NULL, 0);
    up(NET, BL616_FLAG_DEVICE_START, 1, net_credit, pl, sizeof(pl));
    irq_step();
}

static void fill(rt_uint8_t *b, rt_size_t n, unsigned seed)
{
    for (rt_size_t i = 0; i < n; i++)
        b[i] = (rt_uint8_t)(seed * 131 + i * 7 + (i >> 8));
}

static int send_frame(rt_size_t len, unsigned seed)
{
    rt_uint8_t f[1600];

    fill(f, len < sizeof(f) ? len : sizeof(f), seed);
    return wl.ops->wlan_send(wl.dev, f, (int)len);
}

/* a DNLD_DATA message on netdev carrying fill(len, seed) */
static int dn_frame_is(const struct sim_msg *m, rt_size_t len, unsigned seed)
{
    rt_uint8_t f[1600];

    fill(f, len, seed);
    return m && m->len == 8 + len && m->buf[0] == NET && m->buf[5] == BL616_FLAG_DNLD_DATA &&
           (rt_size_t)(m->buf[2] | m->buf[3] << 8) == 4 + len && !memcmp(m->buf + 8, f, len);
}

/* ---------------------------------------------------------------- tests */

static void t_register(void)
{
    CHECK(rt_init_bl616_sdio_init() == RT_EOK, "init must accept 'no card yet'");
    CHECK(sim_driver && sim_driver->id->manufacturer == 0x424c && sim_driver->id->product == 0x0606,
          "driver not registered for 424c:0606");
    sim_reset(0);
    sim_card.sdio_function_num = 0;
    CHECK(probe() == -RT_EINVAL, "card without function 1 accepted");
}

static void t_refused(void)
{
    sim_reset(0);
    sim.status = 0;                  /* the SDIO boot ROM: no application flashed */
    CHECK(probe() == -RT_ENOSYS, "boot ROM status accepted");
    CHECK(!sim.enabled && !sim.irq_attached && !host_worker_alive() && !sim.host_ready,
          "refused probe left the function in use");

    sim_reset(0);
    sim.status = BL616_STATUS_APP_RUN;   /* application without packed RD_LEN */
    CHECK(probe() == -RT_ENOSYS, "unpacked RD_LEN accepted");

    sim_reset(0);
    sim.card_ready_at = 5000;        /* CARD_READY never set within the timeout */
    host_tick = 0;
    CHECK(probe() == -RT_ETIMEOUT, "CARD_READY timeout not reported");
    CHECK(host_tick >= 1000 && host_tick < 1100, "gave up after %u ms, expected 1000",
          (unsigned)host_tick);

    sim_reset(0);
    sim.card_ready_at = host_tick + 30;   /* slow SDU start is waited for */
    sim.status = BL616_STATUS_NOT_SDIO_BOOT | BL616_STATUS_RD_LEN_PACKED;
    CHECK(probe() == RT_EOK, "probe after a slow CARD_READY failed");
    unbind();

    sim_reset(0);
    host_fail_thread_create = 1;
    CHECK(probe() == -RT_ENOMEM, "worker creation failure not reported");
    host_fail_thread_create = 0;
    CHECK(!sim.irq_attached && !sim.enabled, "failed probe left the interrupt attached");
    CHECK(probe() == RT_EOK, "probe after a failed one refused");
    unbind();
    CHECK(sim.violations == 0, "SDU protocol violations");
}

static void t_attach(rt_uint8_t out_ptr)
{
    rt_tick_t t0;

    boot(out_ptr);
    CHECK(sim.host_ready == BL616_UPLD_MAX / 64, "HOST_READY register %u", sim.host_ready);
    CHECK(sim.int_mask == 1 && sim.irq_attached && sim.block_size == 512, "interrupt / block setup");
    CHECK(host_worker_alive() && sim.ndn == 0, "worker must start idle");

    /* the first pass sends HOST_READY on both channels, control first, without waiting for
     * the safety poll; the SDU checks the port order from OUT_PTR */
    t0 = host_tick;
    host_step();
    CHECK(host_tick == t0, "first HOST_READY waited %u ms for the safety poll",
          (unsigned)(host_tick - t0));
    CHECK(sim.ndn == 2 && sim.dn[0].buf[0] == VCH && sim.dn[1].buf[0] == NET &&
          sim.dn[0].buf[5] == BL616_FLAG_HOST_READY && sim.dn[1].buf[5] == BL616_FLAG_HOST_READY,
          "HOST_READY vchan + netdev expected, got %u messages", sim.ndn);
    CHECK(sim.dn[0].len == 8 && sim.dn[0].buf[2] == 4 && sim.dn[0].buf[3] == 0,
          "HOST_READY is an 8-byte message with len 4");
    CHECK(sim.dn[0].port == (unsigned)(out_ptr >> 4) % 4, "first download on port %u", sim.dn[0].port);

    start(8, 3);
    CHECK(wl.registered && wl.mode == RT_WLAN_STATION && !strcmp(wl.dev->device.parent.name, "wlan0"),
          "wlan0 not registered in station mode");
    CHECK((wl.dev->flags & RT_WLAN_FLAG_DIRECT_TX) != 0, "DIRECT_TX flag not set");
    rt_uint8_t got[6] = { 0 };
    wl.ops->wlan_get_mac(wl.dev, got);
    CHECK(!memcmp(got, mac, 6), "MAC from DEVICE_START not used");

    /* the vchan start is answered with GET_STATUS: router + channel + vchan header + 1 byte */
    const struct sim_msg *m = dn_find(0, VCH, BL616_FLAG_DNLD_DATA, 0);
    const rt_uint8_t want[] = { VCH, 0, 9, 0, 0, BL616_FLAG_DNLD_DATA, 0, 0,
                                BL616_VCHAN_USER, 0, 1, 0, WCP_GET_STATUS };
    CHECK(m && m->len == sizeof(want) && !memcmp(m->buf, want, sizeof(want)),
          "GET_STATUS after the vchan start not byte-exact");

    /* more traffic than ports both ways, so both rings wrap from the OUT_PTR start */
    unsigned mark = sim.ndn;
    for (unsigned r = 0; r < 3; r++)
    {
        for (unsigned i = 0; i < 3; i++)
            CHECK(send_frame(100, r * 3 + i) == RT_EOK, "send");
        for (unsigned i = 0; i < 3; i++)
        {
            rt_uint8_t f[100];
            fill(f, sizeof(f), 50 + r * 3 + i);
            up(NET, BL616_FLAG_UPLD_DATA, 1, (rt_uint8_t)(8 + 3 * (r + 1)), f, sizeof(f));
        }
        irq_step();
    }
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 9 && wl.nrx == 9, "9 frames each way, got %u / %u",
          dn_count(mark, NET, BL616_FLAG_DNLD_DATA), wl.nrx);
    CHECK(sim.violations == 0, "SDU protocol violations (out_ptr 0x%02x)", out_ptr);
    unbind();
}

static void t_start_timeout(void)
{
    boot(0);
    host_step();
    CHECK(dn_count(0, NET, BL616_FLAG_HOST_READY) == 1, "first HOST_READY");
    host_advance(999);
    irq_step();
    CHECK(dn_count(0, NET, BL616_FLAG_HOST_READY) == 1, "HOST_READY resent before the timeout");
    host_advance(2);
    irq_step();
    CHECK(dn_count(0, NET, BL616_FLAG_HOST_READY) == 2 && dn_count(0, VCH, BL616_FLAG_HOST_READY) == 2,
          "HOST_READY not resent after 1 s without DEVICE_START");
    /* with nothing else happening the next safety poll, 1 s later, alone brings the resend */
    host_step();
    CHECK(dn_count(0, NET, BL616_FLAG_HOST_READY) == 3, "safety poll does not resend HOST_READY 1 s later");
    host_step();
    CHECK(dn_count(0, NET, BL616_FLAG_HOST_READY) == 4, "one resend per 1 s poll");
    /* data for a channel that has not started is not delivered */
    rt_uint8_t f[60] = { 1 };
    up(NET, BL616_FLAG_UPLD_DATA, 0, 0, f, sizeof(f));
    irq_step();
    CHECK(wl.nrx == 0 && !wl.registered, "frame before DEVICE_START delivered");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
}

static void t_frames(void)
{
    const rt_size_t sizes[] = { 60, 1514, 505, 504, 1 };
    unsigned mark;

    boot(0);
    start(64, 3);
    mark = sim.ndn;
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        CHECK(send_frame(sizes[i], i) == RT_EOK, "send %u", (unsigned)sizes[i]);
    CHECK(send_frame(1515, 9) == -RT_EINVAL && send_frame(0, 9) == -RT_EINVAL,
          "frame sizes outside 1..1514 accepted");
    host_step();
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        CHECK(dn_frame_is(dn_find(mark, NET, BL616_FLAG_DNLD_DATA, i), sizes[i], i),
              "download of %u bytes not byte-exact", (unsigned)sizes[i]);
    /* 60 -> 68 bytes in byte mode, 1514 -> 1522 padded to 3 blocks, 504 -> exactly one block */
    CHECK(dn_find(mark, NET, BL616_FLAG_DNLD_DATA, 0)->xfer == 68 &&
          dn_find(mark, NET, BL616_FLAG_DNLD_DATA, 1)->xfer == 1536 &&
          dn_find(mark, NET, BL616_FLAG_DNLD_DATA, 2)->xfer == 1024 &&
          dn_find(mark, NET, BL616_FLAG_DNLD_DATA, 3)->xfer == 512, "CMD53 sizes");

    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        rt_uint8_t f[1514];
        fill(f, sizes[i], 100 + i);
        up(NET, BL616_FLAG_UPLD_DATA, 0, 0, f, sizes[i]);
        irq_step();
        CHECK(wl.nrx == i + 1 && wl.rx_len[i] == sizes[i] && !memcmp(wl.rx[i], f, sizes[i]),
              "upload of %u bytes not delivered byte-exact (got %u bytes)", (unsigned)sizes[i],
              wl.nrx > i ? (unsigned)wl.rx_len[i] : 0);
    }
    CHECK(sim.int_acks >= 5, "interrupt not acknowledged");

    /* router length beyond the transfer, short messages and unknown tags are dropped */
    rt_uint8_t bad[16] = { NET, 0, 200, 0, 0, BL616_FLAG_UPLD_DATA, 0, 0 };
    CHECK(sim_upload(bad, sizeof(bad)) == 0, "upload");
    rt_uint8_t tiny[6] = { NET, 0, 2, 0, 0, BL616_FLAG_UPLD_DATA };
    CHECK(sim_upload(tiny, sizeof(tiny)) == 0, "upload");
    rt_uint8_t f[60] = { 0 };
    up(7, BL616_FLAG_UPLD_DATA, 0, 0, f, sizeof(f));
    irq_step();
    CHECK(wl.nrx == 5, "malformed upload delivered");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
}

static void t_credit(void)
{
    unsigned mark, sent = 0;
    int fails0 = fails;
    rt_uint8_t limit;

    boot(0);
    start(2, 3);
    mark = sim.ndn;
    for (unsigned i = 0; i < 4; i++)
        CHECK(send_frame(60, i) == RT_EOK, "send");
    host_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 2, "credit 2, %u frames sent",
          dn_count(mark, NET, BL616_FLAG_DNLD_DATA));
    host_step();    /* no credit, no port wait: nothing more */
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 2, "sent beyond the credit");
    up(NET, BL616_FLAG_CREDIT_UPDATE, 1, 4, NULL, 0);
    irq_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 4, "credit update did not resume, %u sent",
          dn_count(mark, NET, BL616_FLAG_DNLD_DATA));
    for (unsigned i = 0; i < 4; i++)
        CHECK(dn_frame_is(dn_find(mark, NET, BL616_FLAG_DNLD_DATA, i), 60, i), "frame %u order", i);
    sent = 4;

    /* through the mod-256 wrap, the credit also riding on data uploads */
    for (unsigned r = 0; r < 100; r++)
    {
        rt_uint8_t f[60] = { 0 };
        unsigned before = sim.ndn;

        for (unsigned i = 0; i < 3; i++)
            CHECK(send_frame(60, sent + i) == RT_EOK, "send round %u", r);
        host_step();
        CHECK(sim.ndn == before, "round %u: sent without credit", r);
        limit = (rt_uint8_t)(sent + 3);
        if (r & 1)
            up(NET, BL616_FLAG_UPLD_DATA, 1, limit, f, sizeof(f));
        else
            up(NET, BL616_FLAG_CREDIT_UPDATE, 1, limit, NULL, 0);
        irq_step();
        CHECK(dn_count(before, NET, BL616_FLAG_DNLD_DATA) == 3, "round %u (limit %u): %u sent", r,
              limit, dn_count(before, NET, BL616_FLAG_DNLD_DATA));
        for (unsigned i = 0; i < 3; i++)
            CHECK(dn_frame_is(dn_find(before, NET, BL616_FLAG_DNLD_DATA, i), 60, sent + i),
                  "round %u frame %u", r, i);
        sent += 3;
        if (fails != fails0)
            break;
    }
    CHECK(sent > 256, "wrap not reached");

    /* DEVICE_START without credit_update: nothing may go until the first update */
    up(NET, BL616_FLAG_DEVICE_START, 0, 0, mac, 6);
    irq_step();
    mark = sim.ndn;
    CHECK(send_frame(60, 1) == RT_EOK, "send");
    host_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 0, "sent before any credit");
    up(NET, BL616_FLAG_CREDIT_UPDATE, 1, 1, NULL, 0);
    irq_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 1, "credit 1 after restart");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
}

static void t_ring_full(void)
{
    unsigned mark;

    boot(0);
    start(0, 0);
    /* the vchan start queued GET_STATUS, which waits for credit: 3 control slots are left */
    for (unsigned i = 0; i < 8; i++)
        CHECK(send_frame(60 + i, i) == RT_EOK, "slot %u refused", i);
    CHECK(send_frame(100, 99) == -RT_EFULL, "9th frame not refused");
    for (unsigned i = 0; i < 3; i++)
        CHECK(wl.ops->wlan_scan(wl.dev, NULL) == RT_EOK, "control slot %u refused", i);
    CHECK(wl.ops->wlan_scan(wl.dev, NULL) == -RT_EFULL, "5th control message not refused");
    host_step();
    mark = sim.ndn;
    up(NET, BL616_FLAG_CREDIT_UPDATE, 1, 20, NULL, 0);
    up(VCH, BL616_FLAG_CREDIT_UPDATE, 1, 20, NULL, 0);
    irq_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 8, "%u of 8 queued frames sent",
          dn_count(mark, NET, BL616_FLAG_DNLD_DATA));
    for (unsigned i = 0; i < 8; i++)
        CHECK(dn_frame_is(dn_find(mark, NET, BL616_FLAG_DNLD_DATA, i), 60 + i, i),
              "queued frame %u overwritten", i);
    CHECK(dn_count(mark, VCH, BL616_FLAG_DNLD_DATA) == 4, "control queue");
    CHECK(dn_find(mark, VCH, BL616_FLAG_DNLD_DATA, 0)->buf[12] == WCP_GET_STATUS &&
          dn_find(mark, VCH, BL616_FLAG_DNLD_DATA, 3)->buf[12] == WCP_SCAN, "control order");
    /* and the ring is usable again */
    CHECK(send_frame(60, 7) == RT_EOK, "ring not freed");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
}

static void t_port_busy(void)
{
    unsigned mark;

    boot(0);
    start(16, 3);
    /* the BL616 stops re-arming: the 4 buffers it has handed over are used, then none */
    sim.auto_arm = 0;
    mark = sim.ndn;
    for (unsigned i = 0; i < 6; i++)
        CHECK(send_frame(60, i) == RT_EOK, "send");
    host_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 4 && host_last_timeout == 1,
          "port busy: %u sent, retry in %d ticks", dn_count(mark, NET, BL616_FLAG_DNLD_DATA),
          (int)host_last_timeout);
    host_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 4, "sent to a port without a buffer");
    sim.wr_bitmap = 1u << (sim.dn_next % 4);
    host_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 5, "one armed port, one frame");
    sim.auto_arm = 1;
    sim.wr_bitmap = 0x0f;
    host_step();
    CHECK(dn_frame_is(dn_find(mark, NET, BL616_FLAG_DNLD_DATA, 5), 60, 5), "last frame after re-arm");
    CHECK(host_last_timeout == 1000, "idle poll back to 1 s");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
}

/* RD_LEN packed form: bit 0 two bytes, bit 1 blocks, count = value >> 2 */
static void t_rd_len(void)
{
    static const struct { rt_uint16_t raw; int two; rt_size_t xfer, len; } ok[] = {
        { 16 << 2, 0, 16, 16 },                 /* one byte, bytes */
        { 1 << 2 | 2, 0, 512, 512 },            /* one byte, one block */
        { 3 << 2 | 2, 0, 1536, 1536 },          /* one byte, 3 blocks */
        { 68 << 2, 1, 68, 68 },                 /* two bytes, bytes */
        { 512 << 2, 1, 512, 512 },
        { 600 << 2, 1, 1024, 600 },             /* bytes above a block move as whole blocks */
        { 1522 << 2, 1, 1536, 1522 },
        { 4 << 2 | 2, 1, 2048, 2048 },          /* two-byte form of a small value */
        { 2048 << 2, 1, 2048, 2048 },           /* exactly our HOST_READY size */
    };
    rt_uint8_t buf[BL616_UPLD_MAX], data[2048];
    rt_size_t len;
    rt_err_t err;

    boot(0x03);
    CHECK(bl616_sdio_read(buf, sizeof(buf), &len) == -RT_EEMPTY, "empty ports not reported");
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
    {
        unsigned hi = sim.rd_len_hi_reads;

        fill(data, ok[i].xfer, i);
        CHECK(sim_upload_raw(ok[i].raw, ok[i].two, data, ok[i].xfer) == 0, "upload");
        len = 0;
        err = bl616_sdio_read(buf, sizeof(buf), &len);
        CHECK(err == RT_EOK && len == ok[i].len && !memcmp(buf, data, ok[i].xfer),
              "RD_LEN 0x%04x: err %d len %u, expected %u", ok[i].raw | ok[i].two, (int)err,
              (unsigned)len, (unsigned)ok[i].len);
        CHECK(sim.rd_len_hi_reads - hi == (unsigned)ok[i].two, "RD_LEN 0x%04x: high byte read %u times",
              ok[i].raw, sim.rd_len_hi_reads - hi);
    }
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();

    /* lengths that cannot be ours: rejected without a transfer (the link is then stuck until
     * both ends reset, by design) */
    static const struct { rt_uint16_t raw; int two; } bad[] = {
        { 0, 0 }, { 0, 1 }, { 5 << 2 | 2, 0 }, { 2049 << 2, 1 }, { 0x3fff << 2 | 2, 1 },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        int e0 = host_log_errors;
        unsigned reads;

        boot(0);
        CHECK(sim_upload_raw(bad[i].raw, bad[i].two, NULL, 0) == 0, "upload");
        reads = sim.up_reads;
        CHECK(bl616_sdio_read(buf, sizeof(buf), &len) == -RT_EIO, "RD_LEN 0x%04x accepted", bad[i].raw);
        CHECK(sim.up_reads == reads && host_log_errors == e0 + 1, "RD_LEN 0x%04x: read or not logged",
              bad[i].raw);
        irq_step();                  /* the worker hits the same port, logs, and goes on */
        CHECK(host_worker_alive() && wl.nrx == 0, "worker after a bad upload");
        unbind();
    }
    CHECK(sim.violations == 0, "SDU protocol violations");
}

static void t_reset(void)
{
    unsigned mark;

    boot(0);
    start(3, 3);
    mark = sim.ndn;
    up(NET, BL616_FLAG_DEVICE_RESET, 0, 0, NULL, 0);
    irq_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_HOST_READY) == 1, "no HOST_READY after DEVICE_RESET");
    CHECK(send_frame(60, 1) == RT_EOK, "send");
    host_step();
    CHECK(dn_count(mark, NET, BL616_FLAG_DNLD_DATA) == 0, "frame sent before the new DEVICE_START");
    rt_uint8_t f[60] = { 0 };
    up(NET, BL616_FLAG_UPLD_DATA, 0, 0, f, sizeof(f));
    irq_step();
    CHECK(wl.nrx == 0, "upload of the old session delivered");
    up(NET, BL616_FLAG_DEVICE_START, 1, 1, mac, 6);
    irq_step();
    CHECK(dn_frame_is(dn_find(mark, NET, BL616_FLAG_DNLD_DATA, 0), 60, 1), "queued frame after restart");
    CHECK(wl.registrations == 1, "wlan0 registered twice");

    mark = sim.ndn;
    up(VCH, BL616_FLAG_DEVICE_STOP, 0, 0, NULL, 0);
    irq_step();
    CHECK(dn_count(mark, VCH, BL616_FLAG_HOST_READY) == 1, "no HOST_READY after DEVICE_STOP");
    up(VCH, BL616_FLAG_DEVICE_START, 1, 3, NULL, 0);
    irq_step();
    const struct sim_msg *m = dn_find(mark, VCH, BL616_FLAG_DNLD_DATA, 0);
    CHECK(m && m->buf[12] == WCP_GET_STATUS, "GET_STATUS after the vchan restart");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
}

static struct wcp_status st(rt_uint8_t state, rt_int8_t rssi)
{
    struct wcp_status s;

    memset(&s, 0, sizeof(s));
    s.type = WCP_STATUS;
    s.state = state;
    s.rssi = rssi;
    s.channel = state == WCP_STATE_UP ? 6 : 0;
    s.ssid_len = 4;
    memcpy(s.ssid, "home", 4);
    return s;
}

static void status(rt_uint8_t state, rt_int8_t rssi)
{
    struct wcp_status s = st(state, rssi);

    up_ctrl(&s, sizeof(s));
    irq_step();
}

static void t_ctrl(void)
{
    static const struct { rt_uint8_t auth; rt_wlan_security_t sec; } auth[] = {
        { WCP_AUTH_OPEN, SECURITY_OPEN }, { WCP_AUTH_WEP, SECURITY_WEP_PSK },
        { WCP_AUTH_WPA_PSK, SECURITY_WPA_AES_PSK }, { WCP_AUTH_WPA2_PSK, SECURITY_WPA2_AES_PSK },
        { WCP_AUTH_WPA_WPA2_PSK, SECURITY_WPA_WPA2_MIXED_PSK },
        { WCP_AUTH_WPA_ENT, SECURITY_WPA_WPA2_MIXED_8021X }, { WCP_AUTH_WPA3_SAE, SECURITY_WPA3_SAE },
        { WCP_AUTH_WPA2_WPA3, SECURITY_WPA2_WPA3_MIXED_PSK }, { WCP_AUTH_UNKNOWN, SECURITY_UNKNOWN },
    };
    unsigned n, mark;

    /* STATUS UP before netdev starts: CONNECT is reported once wlan0 exists */
    boot(0);
    host_step();
    up(VCH, BL616_FLAG_DEVICE_START, 1, 3, NULL, 0);
    irq_step();
    status(WCP_STATE_UP, -40);
    CHECK(wl.nev == 0, "event before wlan0 exists");
    up(NET, BL616_FLAG_DEVICE_START, 1, 8, mac, 6);
    irq_step();
    CHECK(wl.nev == 1 && wl.ev[0] == RT_WLAN_DEV_EVT_CONNECT, "CONNECT at attach");

    status(WCP_STATE_UP, -41);
    CHECK(wl.nev == 1, "second CONNECT");
    CHECK(wl.ops->wlan_get_rssi(wl.dev) == -41, "rssi from the last STATUS");
    status(WCP_STATE_JOINING, 0);
    CHECK(wl.nev == 2 && wl.ev[1] == RT_WLAN_DEV_EVT_DISCONNECT, "UP -> JOINING is DISCONNECT");
    status(WCP_STATE_FAILED, 0);
    CHECK(wl.nev == 3 && wl.ev[2] == RT_WLAN_DEV_EVT_CONNECT_FAIL, "JOINING -> FAILED is CONNECT_FAIL");
    status(WCP_STATE_FAILED, 0);
    CHECK(wl.nev == 3, "repeated FAILED reported again");
    status(WCP_STATE_UP, -50);
    status(WCP_STATE_FAILED, 0);
    CHECK(wl.nev == 5 && wl.ev[3] == RT_WLAN_DEV_EVT_CONNECT && wl.ev[4] == RT_WLAN_DEV_EVT_DISCONNECT,
          "UP -> FAILED is one DISCONNECT");
    status(WCP_STATE_DOWN, 0);
    CHECK(wl.nev == 5, "FAILED -> DOWN reported");

    /* short and malformed control messages */
    struct wcp_status s = st(WCP_STATE_UP, -30);
    up_ctrl(&s, sizeof(s) - 1);
    irq_step();
    rt_uint8_t v[4 + sizeof(s)] = { BL616_VCHAN_USER, 0, (rt_uint8_t)(sizeof(s) + 1), 0 };
    memcpy(v + 4, &s, sizeof(s));
    up(VCH, BL616_FLAG_UPLD_DATA, 0, 0, v, sizeof(v));    /* vchan len beyond the payload */
    v[0] = 2;
    v[2] = sizeof(s);
    up(VCH, BL616_FLAG_UPLD_DATA, 0, 0, v, sizeof(v));    /* not the USER type */
    irq_step();
    CHECK(wl.nev == 5, "malformed STATUS acted on");

    /* scan results */
    mark = wl.nev;
    for (unsigned i = 0; i < sizeof(auth) / sizeof(auth[0]); i++)
    {
        struct wcp_scan_item it;
        memset(&it, 0, sizeof(it));
        it.type = WCP_SCAN_ITEM;
        it.channel = (rt_uint8_t)(1 + i);
        it.rssi = (rt_int8_t)(-30 - i);
        it.auth = auth[i].auth;
        for (unsigned k = 0; k < 6; k++)
            it.bssid[k] = (rt_uint8_t)(0xa0 + i + k);
        it.ssid_len = i == 1 ? 0 : i == 2 ? 40 : 5;     /* hidden; over-long */
        memcpy(it.ssid, i == 2 ? "abcdefghijklmnopqrstuvwxyz0123456789" : "net-X", i == 2 ? 32 : 5);
        up_ctrl(&it, sizeof(it));
        irq_step();
    }
    struct wcp_scan_done d = { WCP_SCAN_DONE, 9 };
    up_ctrl(&d, sizeof(d));
    irq_step();
    n = sizeof(auth) / sizeof(auth[0]);
    CHECK(wl.nev == mark + n + 1 && wl.ev[mark + n] == RT_WLAN_DEV_EVT_SCAN_DONE, "scan events");
    for (unsigned i = 0; i < n && mark + i < wl.nev; i++)
    {
        const struct rt_wlan_info *in = &wl.scan[mark + i];
        CHECK(wl.ev[mark + i] == RT_WLAN_DEV_EVT_SCAN_REPORT, "item %u not a scan report", i);
        CHECK(in->security == auth[i].sec, "auth %u -> security 0x%x", auth[i].auth, (unsigned)in->security);
        CHECK(in->channel == (int)(1 + i) && in->rssi == (int)(-30 - (int)i) &&
              in->band == RT_802_11_BAND_2_4GHZ && in->bssid[0] == 0xa0 + i && in->bssid[5] == 0xa5 + i,
              "item %u fields", i);
        if (i == 1)
            CHECK(in->hidden && in->ssid.len == 0, "hidden SSID");
        else if (i == 2)
            CHECK(in->ssid.len == 32 && !memcmp(in->ssid.val, "abcdefghijklmnopqrstuvwxyz012345", 32),
                  "over-long SSID not clipped to 32");
        else
            CHECK(!in->hidden && in->ssid.len == 5 && !memcmp(in->ssid.val, "net-X", 5), "item %u SSID", i);
    }

    /* K230 -> BL616 commands */
    struct rt_sta_info si;
    memset(&si, 0, sizeof(si));
    si.ssid.len = 4;
    memcpy(si.ssid.val, "home", 4);
    si.key.len = 10;
    memcpy(si.key.val, "secret-psk", 10);
    up(VCH, BL616_FLAG_CREDIT_UPDATE, 1, 100, NULL, 0);
    irq_step();
    unsigned dmark = sim.ndn;
    CHECK(wl.ops->wlan_join(wl.dev, &si) == RT_EOK, "join");
    CHECK(wl.ops->wlan_disconnect(wl.dev) == RT_EOK && wl.ops->wlan_scan(wl.dev, NULL) == RT_EOK, "leave/scan");
    host_step();
    const struct sim_msg *m = dn_find(dmark, VCH, BL616_FLAG_DNLD_DATA, 0);
    struct wcp_join j;
    memset(&j, 0, sizeof(j));
    j.type = WCP_JOIN;
    j.ssid_len = 4;
    j.psk_len = 10;
    memcpy(j.ssid, "home", 4);
    memcpy(j.psk, "secret-psk", 10);
    CHECK(m && m->len == 12 + sizeof(j) && (rt_size_t)(m->buf[2] | m->buf[3] << 8) == 8 + sizeof(j) &&
          m->buf[8] == BL616_VCHAN_USER && m->buf[10] == sizeof(j) && !memcmp(m->buf + 12, &j, sizeof(j)),
          "JOIN not byte-exact");
    m = dn_find(dmark, VCH, BL616_FLAG_DNLD_DATA, 1);
    CHECK(m && m->len == 13 && m->buf[12] == WCP_LEAVE, "LEAVE");
    m = dn_find(dmark, VCH, BL616_FLAG_DNLD_DATA, 2);
    CHECK(m && m->len == 13 && m->buf[12] == WCP_SCAN, "SCAN");
    si.ssid.len = 0;
    CHECK(wl.ops->wlan_join(wl.dev, &si) == -RT_EINVAL, "empty SSID accepted");
    si.ssid.len = 33;
    CHECK(wl.ops->wlan_join(wl.dev, &si) == -RT_EINVAL, "33-byte SSID accepted");
    si.ssid.len = 4;
    si.key.len = 65;
    CHECK(wl.ops->wlan_join(wl.dev, &si) == -RT_EINVAL, "65-byte key accepted");
    CHECK(wl.ops->wlan_mode(wl.dev, RT_WLAN_AP) == -RT_ENOSYS, "AP mode accepted");
    CHECK(sim.violations == 0, "SDU protocol violations");
    unbind();
    CHECK(!wl.registered, "wlan0 not unregistered at remove");
    CHECK(send_frame(60, 0) == -RT_EIO, "send after remove accepted");
}

int main(void)
{
    t_register();
    t_refused();
    t_attach(0x00);
    t_attach(0x21);   /* warm start: the BL616 counters are not at 0 */
    t_attach(0x33);
    t_start_timeout();
    t_frames();
    t_credit();
    t_ring_full();
    t_port_busy();
    t_rd_len();
    t_reset();
    t_ctrl();
    if (fails)
        printf("bl616_nethub: FAIL (%d)\n", fails);
    else
        printf("bl616_nethub: probe, handshake, ports from OUT_PTR, frames both ways, credits, "
               "rings, RD_LEN, resets, control messages all pass\n");
    return fails != 0;
}
