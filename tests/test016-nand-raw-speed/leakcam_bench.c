/*
 * TEMPORARY (LEAKCAM tests/test015 DDR bandwidth, tests/test016 NAND raw speed), SDK copy only.
 * DELETE after the tests: install.sh does not remove it (board/SConscript builds every .c here).
 *
 *   ddr_bench [KB]            write (memset), read (64-bit sum), copy (memcpy) over two buffers of KB each
 *   nand_bench read [blocks]  raw page reads from nand0 (/bin, read only), checksum to compare across clocks
 *   nand_bench write [blocks] erase, program, read back and verify in the free last 8 MiB (blocks 1984..2047)
 *   nand_clk [hz]             show / set the NAND QSPI clock (Kconfig SPI_NAND_SPI_FREQ at boot)
 *   nand_rxsd [n]             show / set SPI0 RX_SAMPLE_DLY (ssi_clk cycles; Kconfig RT_SPI0_RXSD at boot)
 *   pad_ds [ds [first last]]  show / set the pad drive strength (0..15) of IO14-19 (OSPI CS, CLK, D0-D3) only
 *   boot_times                tests/test021: rdtime (from reset release) at the end of each init level
 *
 * Timing: rdtime, 27 MHz. Every loop sleeps 1 ms now and then (outside the timed part): the 20 s watchdog's
 * feeder runs at priority 25 (skill k230-rtsmart-hangs).
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include "drivers/mtd_nand.h"
#include "board.h"
#include "../drivers/interdrv/spi/drv_spi.h"
#include "../drivers/interdrv/sysctl/sysctl_clock/sysctl_clk.h"
#include <ioremap.h>
#include "../drivers/interdrv/fpioa/drv_fpioa.h"

static inline rt_uint64_t now(void)
{
    rt_uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t;
}

/* bytes over 27 MHz ticks as MB/s (10^6 B/s) with one decimal */
static void rate(const char *what, rt_uint64_t bytes, rt_uint64_t ticks)
{
    rt_uint64_t us = ticks / 27, r10 = us ? bytes * 10 / us : 0;
    rt_kprintf("  %-28s %8u KB in %6u us: %4u.%u MB/s\n", what, (unsigned)(bytes / 1024), (unsigned)us,
               (unsigned)(r10 / 10), (unsigned)(r10 % 10));
}

static int ddr_bench(int argc, char **argv)
{
    rt_size_t kb = argc > 1 ? (rt_size_t)atoi(argv[1]) : 1024, size = kb * 1024;
    rt_size_t reps = size >= (64u << 20) ? 1 : (64u << 20) / size, r, i;
    rt_uint64_t tw = 0, tr = 0, tc = 0, t;
    volatile rt_uint64_t sink = 0;
    rt_uint8_t *a = rt_malloc_align(size, 64), *b = rt_malloc_align(size, 64);

    if (!a || !b)
    {
        rt_kprintf("ddr_bench: no 2 x %u KB in the kernel heap (8 MB)\n", (unsigned)kb);
        if (a)                               /* rt_free_align(NULL) faults (2026-10-21) */
            rt_free_align(a);
        if (b)
            rt_free_align(b);
        return -1;
    }
    for (i = 0; i < size; i++)
        a[i] = (rt_uint8_t)(i * 7);
    for (r = 0; r < reps; r++)
    {
        t = now();
        memset(b, (int)r, size);
        tw += now() - t;
        t = now();
        {
            const rt_uint64_t *p = (const rt_uint64_t *)a;
            rt_uint64_t s = 0;
            for (i = 0; i < size / 8; i += 4)
                s += p[i] + p[i + 1] + p[i + 2] + p[i + 3];
            sink += s;
        }
        tr += now() - t;
        t = now();
        memcpy(b, a, size);
        tc += now() - t;
        if ((r & 15) == 15)
            rt_thread_mdelay(1);
    }
    {   /* the CPU clock: rdcycle against rdtime (27 MHz) over ~100 ms */
        rt_uint64_t c0, c1, t0 = now(), t1;
        __asm__ volatile("rdcycle %0" : "=r"(c0));
        while (now() - t0 < 2700000u)
            ;
        __asm__ volatile("rdcycle %0" : "=r"(c1));
        t1 = now();
        rt_kprintf("ddr_bench: CPU clock %u MHz (rdcycle over rdtime)\n", (unsigned)((c1 - c0) * 27 / (t1 - t0)));
    }
    rt_kprintf("ddr_bench: 2 x %u KB, %u passes (cached)\n", (unsigned)kb, (unsigned)reps);
    rate("write (memset)", (rt_uint64_t)size * reps, tw);
    rate("read (64-bit sum)", (rt_uint64_t)size * reps, tr);
    rate("copy (memcpy, bytes moved)", (rt_uint64_t)size * reps, tc);
    rt_kprintf("  check %u (ignore)\n", (unsigned)(sink & 0xffff));
    rt_free_align(a);
    rt_free_align(b);
    return 0;
}
MSH_CMD_EXPORT(ddr_bench, test015: DDR bandwidth [KB per buffer]);

#define SPARE_FIRST 1984u                    /* 248 MiB / 128 KiB: the free area after the image */
#define SPARE_LAST  2047u

static int nand_bench(int argc, char **argv)
{
    struct rt_mtd_nand_device *d0 = (struct rt_mtd_nand_device *)rt_device_find("nand0");
    struct rt_mtd_nand_device *d1 = (struct rt_mtd_nand_device *)rt_device_find("nand1");
    int write = argc > 1 && strcmp(argv[1], "write") == 0;
    rt_uint32_t blocks = argc > 2 ? (rt_uint32_t)atoi(argv[2]) : 64, b, p, ppb, ps, bad = 0, errs = 0, crc = 0;
    rt_uint64_t te = 0, tw = 0, tr = 0, bytes = 0, t;
    rt_uint8_t *buf, *pat;

    if (!d0 || !d1)
    {
        rt_kprintf("nand_bench: nand0/nand1 not found\n");
        return -1;
    }
    ppb = d0->pages_per_block;
    ps = d0->page_size;
    buf = rt_malloc_align(ps, 64);
    pat = rt_malloc_align(ps, 64);
    if (!buf || !pat)
        return -1;

    if (!write)
    {
        if (blocks > d0->block_total)
            blocks = d0->block_total;
        for (b = d0->block_start; b < d0->block_start + blocks; b++)
        {
            for (p = 0; p < ppb; p++)
            {
                t = now();
                rt_err_t e = rt_mtd_nand_read(d0, b * ppb + p, buf, ps, RT_NULL, 0);
                tr += now() - t;
                if (e != RT_EOK)
                    errs++;
                for (rt_uint32_t i = 0; i < ps; i += 4)
                    crc = (crc << 1 | crc >> 31) ^ *(rt_uint32_t *)(buf + i);
                bytes += ps;
            }
            rt_thread_mdelay(1);
        }
        rt_kprintf("nand_bench read: nand0 blocks %u..%u, %u read errors, checksum %08x\n",
                   (unsigned)d0->block_start, (unsigned)(d0->block_start + blocks - 1), (unsigned)errs, (unsigned)crc);
        rate("raw page read (2 KiB pages)", bytes, tr);
    }
    else
    {
        struct rt_mtd_nand_device sp = *d1;     /* the free area, never registered: UFFS cannot see it */
        sp.block_start = SPARE_FIRST;
        sp.block_end = SPARE_LAST;
        sp.block_total = SPARE_LAST - SPARE_FIRST + 1;
        if (blocks > sp.block_total)
            blocks = sp.block_total;
        for (b = SPARE_FIRST; b < SPARE_FIRST + blocks; b++)
        {
            if (sp.ops->check_block && sp.ops->check_block(&sp, b) != RT_EOK)
            {
                bad++;
                continue;
            }
            t = now();
            if (sp.ops->erase_block(&sp, b) != RT_EOK)
                errs++;
            te += now() - t;
            for (p = 0; p < ppb; p++)
            {
                for (rt_uint32_t i = 0; i < ps; i++)
                    pat[i] = (rt_uint8_t)(b * 31 + p * 7 + i);
                t = now();
                if (rt_mtd_nand_write(&sp, b * ppb + p, pat, ps, RT_NULL, 0) != RT_EOK)
                    errs++;
                tw += now() - t;
                t = now();
                if (rt_mtd_nand_read(&sp, b * ppb + p, buf, ps, RT_NULL, 0) != RT_EOK || memcmp(buf, pat, ps))
                    errs++;
                tr += now() - t;
                bytes += ps;
            }
            rt_thread_mdelay(1);
        }
        rt_kprintf("nand_bench write: free blocks %u..%u, %u bad (skipped), %u errors (erase/program/verify)\n",
                   SPARE_FIRST, (unsigned)(SPARE_FIRST + blocks - 1), (unsigned)bad, (unsigned)errs);
        {
            rt_uint32_t n = blocks - bad;
            rt_kprintf("  erase                        %u blocks in %u us: %u us per 128 KiB block\n", (unsigned)n,
                       (unsigned)(te / 27), (unsigned)(n ? te / 27 / n : 0));
        }
        rate("program (2 KiB pages)", bytes, tw);
        rate("read back", bytes, tr);
    }
    rt_free_align(buf);
    rt_free_align(pat);
    return 0;
}
MSH_CMD_EXPORT(nand_bench, test016: raw NAND speed: read [blocks] | write [blocks]);

static dw_spi_reg_t *spi0_regs(void)
{
    static dw_spi_reg_t *r;
    if (!r)
        r = (dw_spi_reg_t *)rt_ioremap((void *)SPI_OPI_BASE_ADDR, sizeof(dw_spi_reg_t));
    return r;
}

static int nand_clk(int argc, char **argv)
{
    rt_device_t dev = rt_device_find("spinand0");
    if (!dev)
    {
        rt_kprintf("nand_clk: spinand0 not found\n");
        return -1;
    }
    if (argc > 1)
    {
        struct rt_qspi_configuration cfg = {
            .parent.mode = 0,
            .parent.hard_cs = 0x80 | (1 << 0),
            .parent.soft_cs = 0x00,
            .parent.data_width = 8,
            .parent.max_hz = (rt_uint32_t)atoi(argv[1]),
            .ddr_mode = 0,
            .medium_size = 0,
            .qspi_dl_width = SPI_NAND_SPI_BUS_WIDTH,
        };
        rt_kprintf("nand_clk: configure %u Hz: %d\n", (unsigned)cfg.parent.max_hz,
                   (int)rt_qspi_configure((struct rt_qspi_device *)dev, &cfg));
    }
    rt_kprintf("nand_clk: %u Hz requested, %d-bit (boot default %d Hz)\n",
               (unsigned)((struct rt_spi_device *)dev)->config.max_hz, SPI_NAND_SPI_BUS_WIDTH, SPI_NAND_SPI_FREQ);
    {   /* what the controller really runs: ssi_clk / BAUDR (DW SSI uses even dividers) */
        dw_spi_reg_t *r = spi0_regs();
        rt_uint32_t src = sysctl_clk_get_leaf_freq(SYSCTL_CLK_SSI_0_CLK), dv = r->baudr;
        rt_kprintf("nand_clk: ssi_clk %u Hz, BAUDR %u -> SCLK %u Hz, RX_SAMPLE_DLY 0x%x\n", (unsigned)src, (unsigned)dv,
                   (unsigned)(dv ? src / dv : 0), (unsigned)r->rx_sample_delay);
    }
    return 0;
}
MSH_CMD_EXPORT(nand_clk, test016: show or set the NAND QSPI clock [hz]);

/* The driver writes RX_SAMPLE_DLY only in configure (from RT_SPI0_RXSD), so a value set here holds until the next
 * nand_clk. Bit 16 (RSD edge) stays 0. */
static int nand_rxsd(int argc, char **argv)
{
    dw_spi_reg_t *r = spi0_regs();
    if (argc > 1)
    {
        r->ssienr = 0;
        r->rx_sample_delay = (rt_uint32_t)atoi(argv[1]) & 0xffu;
    }
    rt_kprintf("nand_rxsd: RX_SAMPLE_DLY %u ssi_clk cycles (boot default %d)\n", (unsigned)(r->rx_sample_delay & 0xffu),
               RT_SPI0_RXSD);
    return 0;
}
MSH_CMD_EXPORT(nand_rxsd, test016: show or set the NAND SPI read sample delay [cycles]);

/* test021 boot time: rdtime counts from the SoC reset, so these are times after reset release (ROM, SPL, DDR
 * training and OpenSBI are all before "kernel init start"). The sections sort by name: "N.9" runs last in level N. */
static rt_uint64_t bt[8];
static const char *const bt_name[8] = {
    "kernel init start (after ROM, SPL, DDR, OpenSBI)", "board init done", "prev init done", "device init done",
    "component init done", "env init done (/bin, /sdcard mounted)", "app init done", "first boot_times call"};
static int bt0(void) { bt[0] = now(); return 0; }
static int bt1(void) { bt[1] = now(); return 0; }
static int bt2(void) { bt[2] = now(); return 0; }
static int bt3(void) { bt[3] = now(); return 0; }
static int bt4(void) { bt[4] = now(); return 0; }
static int bt5(void) { bt[5] = now(); return 0; }
static int bt6(void) { bt[6] = now(); return 0; }
INIT_EXPORT(bt0, "0.end.0");
INIT_EXPORT(bt1, "1.9");
INIT_EXPORT(bt2, "2.9");
INIT_EXPORT(bt3, "3.9");
INIT_EXPORT(bt4, "4.9");
INIT_EXPORT(bt5, "5.9");
INIT_EXPORT(bt6, "6.9");

static int boot_times(int argc, char **argv)
{
    int i;
    rt_uint64_t t = now();

    if (!bt[7])
        bt[7] = t;
    for (i = 0; i < 8; i++)
        rt_kprintf("boot_times %u.%03u s  %s\n", (unsigned)(bt[i] / 27000000u), (unsigned)(bt[i] / 27000u % 1000u),
                   bt_name[i]);
    rt_kprintf("boot_times %u.%03u s  now\n", (unsigned)(t / 27000000u), (unsigned)(t / 27000u % 1000u));
    return 0;
}
MSH_CMD_EXPORT(boot_times, test021: init level times after reset release);

/* test016: OSPI pad drive. Only the ds field changes; msc (bank voltage) is checked unchanged after the write. */
static int pad_ds(int argc, char **argv)
{
    int first = argc > 3 ? atoi(argv[2]) : 14, last = argc > 3 ? atoi(argv[3]) : 19, pin;

    if (first < 14 || last > 19 || first > last)
    {
        rt_kprintf("pad_ds: OSPI pins only (14..19)\n");
        return -1;
    }
    for (pin = first; pin <= last; pin++)
    {
        fpioa_iomux_cfg_t c, n;
        drv_fpioa_get_pin_cfg(pin, &c.u.value);
        if (argc > 1)
        {
            n = c;
            n.u.bit.ds = (uint32_t)atoi(argv[1]) & 0xfu;
            drv_fpioa_set_pin_cfg(pin, n.u.value & 0x7fffffffu);
            drv_fpioa_get_pin_cfg(pin, &n.u.value);
            if (n.u.bit.msc != c.u.bit.msc)
                rt_kprintf("pad_ds: IO%d msc CHANGED %u -> %u\n", pin, (unsigned)c.u.bit.msc, (unsigned)n.u.bit.msc);
            c = n;
        }
        rt_kprintf("pad_ds: IO%d ds %u msc %u sel %u (0x%08x)\n", pin, (unsigned)c.u.bit.ds, (unsigned)c.u.bit.msc,
                   (unsigned)c.u.bit.io_sel, (unsigned)(c.u.value & 0x7fffffffu));
    }
    return 0;
}
MSH_CMD_EXPORT(pad_ds, test016: show or set the OSPI pad drive [ds [first last]]);
