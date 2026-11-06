/*
 * /dev/ota: the K230 image's A/B update on SPI NAND (DESIGN.md 5.3.7, tests/test130); the interface in ota_nand.h.
 *
 * The SPL (U-Boot board/kendryte/common/k230_boot.c, + the LEAKCAM NAND patch in install.sh) boots the slot whose
 * ota_meta record is valid with the higher version (A on a tie or with none), checks the slot image's SHA-256 (its
 * K230 firmware header) and boots the other slot when it fails. It loads a slot page by page and skips a bad block
 * whole: this writer fills the slot the same way. The record goes last: until it is written, the old slot boots.
 *
 * The NAND partitions rtt_a, rtt_b and ota_meta come from spinand_parts.h (raw MTD devices, never mounted); pages and
 * blocks are numbered across the chip.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include <dfs_posix.h>

#ifdef RT_USING_USERSPACE
#include <lwp_user_mm.h>
#endif

#include "ota_nand.h"
#include "ota_sha256.h"

#define REC_LEN 512                     /* struct ota_slot_meta of the SPL */
#define HDR_LEN 0x210                   /* the K230 firmware header (firmware_head_s): magic, length, crypto, verify */

static struct rt_device ota;
static struct rt_mtd_nand_device *slot_dev, *meta_dev;
static char slot;                       /* the slot being written, 0 = none begun */
static rt_uint8_t *page_buf;            /* one page: the write's partial page, the read's current page */
static rt_uint8_t *meta_buf;            /* one page for the records (never the write's page) */
static rt_uint32_t page_fill;           /* bytes in page_buf waiting to be written */
static rt_uint32_t wr_block, wr_page;   /* the next page to write: its block (chip-wide) and page in the block */
static rt_uint32_t rd_block, rd_page, rd_off;   /* the read-back position; rd_off = bytes used of page_buf */
static rt_bool_t rd_loaded;

static rt_uint32_t crc32_ieee(const rt_uint8_t *p, rt_uint32_t n)
{
    rt_uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static struct rt_mtd_nand_device *mtd(const char *name)
{
    return (struct rt_mtd_nand_device *)rt_device_find(name);
}

/* slot 'A' or 'B''s record: its version if valid (magic + CRC as the SPL checks them) */
static rt_bool_t record(char s, rt_uint32_t *version)
{
    rt_uint8_t *r = meta_buf;
    rt_uint32_t block = meta_dev->block_start + (s == 'B' ? meta_dev->block_total / 2 : 0);
    if (rt_mtd_nand_check_block(meta_dev, block) != 0 ||
        rt_mtd_nand_read(meta_dev, block * meta_dev->pages_per_block, r, meta_dev->page_size, RT_NULL, 0) != RT_EOK)
        return RT_FALSE;
    rt_uint32_t magic, crc, zero = 0;
    memcpy(&magic, r, 4);
    memcpy(version, r + 4, 4);
    memcpy(&crc, r + 8, 4);
    memcpy(r + 8, &zero, 4);
    return magic == OTA_META_MAGIC && crc32_ieee(r, REC_LEN) == crc;
}

/* the SPL's choice (k230_select_boot_slot) */
static char booting(rt_uint32_t *version)
{
    rt_uint32_t va = 0, vb = 0;
    rt_bool_t a = record('A', &va), b = record('B', &vb);
    char s = (b && (!a || vb > va)) ? 'B' : 'A';
    *version = s == 'B' ? vb : (a ? va : 0);
    return s;
}

/* the next good block of the slot from block (erased when erase), or 0 past its end */
static rt_uint32_t good_block(rt_uint32_t block, rt_bool_t erase)
{
    for (; block <= slot_dev->block_end; block++) {
        if (rt_mtd_nand_check_block(slot_dev, block) != 0)
            continue;                   /* bad: the SPL skips it too */
        if (!erase || rt_mtd_nand_erase_block(slot_dev, block) == RT_EOK)
            return block;
        rt_kprintf("ota: erase of block %u failed: marked bad\n", block);
        rt_mtd_nand_mark_badblock(slot_dev, block);
    }
    return 0;
}

static rt_err_t write_page(void)
{
    if (wr_page == slot_dev->pages_per_block) {
        wr_block = good_block(wr_block + 1, RT_TRUE);
        wr_page = 0;
        if (!wr_block)
            return -RT_EFULL;           /* the image does not fit the slot's good blocks */
    }
    memset(page_buf + page_fill, 0xff, slot_dev->page_size - page_fill);
    rt_err_t r = rt_mtd_nand_write(slot_dev, wr_block * slot_dev->pages_per_block + wr_page, page_buf,
                                   slot_dev->page_size, RT_NULL, 0);
    wr_page++;
    page_fill = 0;
    return r;
}

static rt_size_t ota_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    (void)dev;
    (void)pos;                          /* appended: the image is written in order */
    if (!slot) {
        rt_set_errno(-RT_ERROR);
        return 0;
    }
    const rt_uint8_t *p = buf;
    rt_size_t done = 0;
    while (done < size) {
        rt_uint32_t n = slot_dev->page_size - page_fill;
        if (n > size - done)
            n = size - done;
        memcpy(page_buf + page_fill, p + done, n);
        page_fill += n;
        done += n;
        if (page_fill == slot_dev->page_size && write_page() != RT_EOK) {
            rt_kprintf("ota: write failed at block %u page %u\n", wr_block, wr_page);
            rt_set_errno(-RT_EIO);
            return 0;
        }
    }
    return done;
}

static rt_size_t ota_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    (void)dev;
    (void)pos;                          /* from the slot's start after the flush, in order */
    if (!slot)
        return 0;
    rt_uint8_t *p = buf;
    rt_size_t done = 0;
    while (done < size) {
        if (!rd_loaded || rd_off == slot_dev->page_size) {
            if (rd_loaded && ++rd_page == slot_dev->pages_per_block) {
                rd_block = good_block(rd_block + 1, RT_FALSE);
                rd_page = 0;
            }
            if (!rd_block || rt_mtd_nand_read(slot_dev, rd_block * slot_dev->pages_per_block + rd_page, page_buf,
                                              slot_dev->page_size, RT_NULL, 0) != RT_EOK)
                break;
            rd_loaded = RT_TRUE;
            rd_off = 0;
        }
        rt_uint32_t n = slot_dev->page_size - rd_off;
        if (n > size - done)
            n = size - done;
        memcpy(p + done, page_buf + rd_off, n);
        rd_off += n;
        done += n;
    }
    return done;
}

static rt_err_t commit(rt_uint32_t version)
{
    rt_uint32_t block = meta_dev->block_start + (slot == 'B' ? meta_dev->block_total / 2 : 0);
    rt_uint32_t magic = OTA_META_MAGIC, crc, zero = 0;
    rt_uint8_t *r = meta_buf;
    memset(r, 0, REC_LEN);
    memset(r + REC_LEN, 0xff, meta_dev->page_size - REC_LEN);
    memcpy(r, &magic, 4);
    memcpy(r + 4, &version, 4);
    memcpy(r + 8, &zero, 4);
    crc = crc32_ieee(r, REC_LEN);
    memcpy(r + 8, &crc, 4);
    if (rt_mtd_nand_check_block(meta_dev, block) != 0 || rt_mtd_nand_erase_block(meta_dev, block) != RT_EOK ||
        rt_mtd_nand_write(meta_dev, block * meta_dev->pages_per_block, r, meta_dev->page_size, RT_NULL, 0) !=
            RT_EOK)
        return -RT_EIO;
    rt_uint32_t back;
    return record(slot, &back) && back == version ? RT_EOK : -RT_EIO;
}

/* the inactive slot, positions at its start; b: the slot, the one that boots, the version to commit */
static rt_err_t begin(struct ota_begin *b)
{
    rt_uint32_t v;
    b->boots = booting(&v);
    b->slot = b->boots == 'A' ? 'B' : 'A';
    b->version = v + 1;
    slot_dev = mtd(b->slot == 'A' ? "rtt_a" : "rtt_b");
    wr_block = slot_dev ? good_block(slot_dev->block_start, RT_TRUE) : 0;
    if (!wr_block)
        return -RT_EIO;
    slot = b->slot;
    wr_page = page_fill = 0;
    rd_block = good_block(slot_dev->block_start, RT_FALSE);
    rd_page = rd_off = 0;
    rd_loaded = RT_FALSE;
    rt_kprintf("ota: writing slot %c (slot %c boots, version %u -> %u)\n", b->slot, b->boots, v, b->version);
    return RT_EOK;
}

static rt_err_t flush(void)
{
    return slot && (page_fill == 0 || write_page() == RT_EOK) ? RT_EOK : -RT_EIO;
}

static rt_err_t ota_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev;
    switch (cmd) {
    case OTA_IOC_BEGIN: {
        struct ota_begin b;
        if (begin(&b) != RT_EOK)
            return -RT_EIO;
#ifdef RT_USING_USERSPACE
        return lwp_put_to_user(args, &b, sizeof(b)) == sizeof(b) ? RT_EOK : -RT_EINVAL;
#else
        memcpy(args, &b, sizeof(b));
        return RT_EOK;
#endif
    }
    case OTA_IOC_FLUSH:
        return flush();
    case OTA_IOC_COMMIT: {
        rt_uint32_t v;
#ifdef RT_USING_USERSPACE
        if (lwp_get_from_user(&v, args, sizeof(v)) != sizeof(v))
            return -RT_EINVAL;
#else
        memcpy(&v, args, sizeof(v));
#endif
        if (!slot)
            return -RT_ERROR;
        rt_err_t r = commit(v);
        rt_kprintf("ota: slot %c record version %u %s\n", slot, v, r == RT_EOK ? "written" : "FAILED");
        slot = 0;
        return r;
    }
    case OTA_IOC_RECORDS: {
        struct ota_records r;
        r.valid_a = record('A', &r.version_a);
        r.valid_b = record('B', &r.version_b);
#ifdef RT_USING_USERSPACE
        return lwp_put_to_user(args, &r, sizeof(r)) == sizeof(r) ? RT_EOK : -RT_EINVAL;
#else
        memcpy(args, &r, sizeof(r));
        return RT_EOK;
#endif
    }
    }
    return -RT_ENOSYS;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops ota_ops = { RT_NULL, RT_NULL, RT_NULL, ota_read, ota_write, ota_control };
#endif

static int ota_nand_init(void)
{
    meta_dev = mtd("ota_meta");
    struct rt_mtd_nand_device *a = mtd("rtt_a");
    if (!meta_dev || !a) {
        rt_kprintf("ota: no rtt_a / ota_meta MTD partition (spinand_parts.h): no /dev/ota\n");
        return -RT_ERROR;
    }
    page_buf = rt_malloc(a->page_size);
    meta_buf = rt_malloc(meta_dev->page_size);
    if (!page_buf || !meta_buf)
        return -RT_ENOMEM;
    ota.type = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    ota.ops = &ota_ops;
#else
    ota.read = ota_read;
    ota.write = ota_write;
    ota.control = ota_control;
#endif
    return rt_device_register(&ota, "ota", RT_DEVICE_FLAG_RDWR);
}
INIT_APP_EXPORT(ota_nand_init);         /* after the SPI NAND (its partitions are registered at device init) */

/* msh: both slot records as the SPL reads them, and the slot it boots */
static int ota_records(void)
{
    rt_uint32_t va = 0, vb = 0, v;
    if (!meta_dev || !meta_buf) {
        rt_kprintf("ota_records: no /dev/ota\n");
        return -1;
    }
    rt_bool_t a = record('A', &va), b = record('B', &vb);
    rt_kprintf("ota_records: A %s %u, B %s %u: slot %c boots\n", a ? "valid" : "none", va, b ? "valid" : "none", vb,
               booting(&v));
    return 0;
}
MSH_CMD_EXPORT(ota_records, the A/B slot records and the slot that boots);

/* msh: a slot image from a file into the inactive slot, as an update does (tests/test130): written, read back, its
 * SHA-256 against the one in its own K230 header (what the SPL checks), then the record. No network needed. */
static int ota_file(int argc, char **argv)
{
    static rt_uint8_t buf[4096];
    rt_uint8_t hdr[44], sum[32];
    struct ota_begin b;
    struct ota_sha256 s;
    rt_uint32_t len = 0, total = 0;
    if (argc != 2) {
        rt_kprintf("usage: ota_file <slot image, e.g. /sdcard/opensbi_rtt_system.bin>\n");
        return -1;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0 || read(fd, hdr, sizeof(hdr)) != sizeof(hdr) || memcmp(hdr, "K230", 4) != 0) {
        rt_kprintf("ota_file: %s: not a K230 slot image\n", argv[1]);
        if (fd >= 0)
            close(fd);
        return -1;
    }
    memcpy(&len, hdr + 4, 4);
    lseek(fd, 0, SEEK_SET);
    rt_tick_t t0 = rt_tick_get();
    if (begin(&b) != RT_EOK) {
        close(fd);
        return -1;
    }
    for (int n; (n = read(fd, buf, sizeof(buf))) > 0; total += n)
        if (ota_write(&ota, 0, buf, n) != (rt_size_t)n) {
            rt_kprintf("ota_file: write failed at %u B\n", total);
            close(fd);
            slot = 0;
            return -1;
        }
    close(fd);
    if (total != HDR_LEN + len || flush() != RT_EOK) {
        rt_kprintf("ota_file: %u B written, the header says %u: not committed\n", total, HDR_LEN + len);
        slot = 0;
        return -1;
    }
    rt_tick_t t1 = rt_tick_get();
    ota_sha256_init(&s);
    for (rt_uint32_t done = 0; done < total;) {   /* the header first (skipped by the sum), then the payload */
        rt_uint32_t n = total - done < sizeof(buf) ? total - done : sizeof(buf);
        if (ota_read(&ota, 0, buf, n) != n)
            break;
        rt_uint32_t from = done < HDR_LEN ? (HDR_LEN - done < n ? HDR_LEN - done : n) : 0;
        ota_sha256_update(&s, buf + from, n - from);
        done += n;
    }
    ota_sha256_final(&s, sum);
    if (memcmp(sum, hdr + 12, 32) != 0) {
        rt_kprintf("ota_file: slot %c read back does not match its header's SHA-256: not committed\n", slot);
        slot = 0;
        return -1;
    }
    rt_uint32_t v = b.version;
    rt_err_t r = commit(v);
    slot = 0;
    rt_kprintf("ota_file: %u B into slot %c in %u ms, read back and SHA-256 ok in %u ms; record %u %s: slot %c boots "
               "next\n", total, b.slot, (unsigned)((t1 - t0) * 1000 / RT_TICK_PER_SECOND),
               (unsigned)((rt_tick_get() - t1) * 1000 / RT_TICK_PER_SECOND), v, r == RT_EOK ? "written" : "FAILED",
               r == RT_EOK ? b.slot : b.boots);
    return r == RT_EOK ? 0 : -1;
}
MSH_CMD_EXPORT(ota_file, a slot image from a file into the inactive slot and its record: ota_file <path>);
