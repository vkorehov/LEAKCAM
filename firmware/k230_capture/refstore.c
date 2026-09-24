#define _GNU_SOURCE
#include "refstore.h"
#include "imgdiff.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define REF_MAGIC   0x5453454Cu     /* "LEST" little-endian */
#define REF_VERSION 2u      /* 2: two slots per reference, generation counter */

struct ref_header {
    uint32_t magic, version;
    uint16_t width, height;
    uint32_t payload_len;
    int64_t  taken;
    uint32_t crc32;
    uint32_t gen;               /* bumped on every save; the newer valid slot wins */
};

uint32_t refstore_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void path_of(char *buf, size_t n, const char *dir, int cam, enum ref_kind kind, int slot)
{
    static const char *const ext[] = { [REF_LAST] = "last", [REF_BASE] = "base", [REF_HIST] = "hist" };
    snprintf(buf, n, "%s/cam%d.%s.%d", dir, cam, ext[kind], slot);
}

static int read_all(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* one slot: 0 = valid (header in *h, payload in img), 1 = absent or damaged */
static int load_slot(const char *path, struct ref_header *h, uint8_t *img)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 1;
    int ok = read_all(fd, h, sizeof(*h)) == 0 && h->magic == REF_MAGIC &&
             h->version == REF_VERSION && h->width == IMGDIFF_W && h->height == IMGDIFF_H &&
             h->payload_len == IMGDIFF_W * IMGDIFF_H && read_all(fd, img, h->payload_len) == 0 &&
             refstore_crc32(img, h->payload_len) == h->crc32;
    close(fd);
    if (!ok)
        fprintf(stderr, "refstore: %s is damaged or from another version, ignoring it\n", path);
    return ok ? 0 : 1;
}

/* newest valid slot; *gen = its generation, *slot = its index (or -1) */
static int load_best(const char *dir, int cam, enum ref_kind kind, uint8_t *img, time_t *taken,
                     uint32_t *gen, int *slot)
{
    static uint8_t tmp[IMGDIFF_W * IMGDIFF_H];
    struct ref_header h;
    int best = -1;
    uint32_t best_gen = 0;
    for (int s = 0; s < 2; s++) {
        char path[256];
        path_of(path, sizeof(path), dir, cam, kind, s);
        if (load_slot(path, &h, tmp) != 0)
            continue;
        /* wrap-safe "newer": a slot is at most one generation ahead of the other */
        if (best < 0 || (int32_t)(h.gen - best_gen) > 0) {
            best = s;
            best_gen = h.gen;
            memcpy(img, tmp, sizeof(tmp));
            if (taken)
                *taken = (time_t)h.taken;
        }
    }
    *gen = best_gen;
    *slot = best;
    return best < 0 ? 1 : 0;
}

int refstore_load(const char *dir, int cam, enum ref_kind kind, uint8_t *img, time_t *taken)
{
    uint32_t gen;
    int slot;
    return load_best(dir, cam, kind, img, taken, &gen, &slot);
}

/*
 * Write into the slot that does NOT hold the newest valid copy. UFFS (the RT-Smart NAND
 * filesystem) refuses rename() onto an existing name, so the old slot file is unlinked first;
 * a power cut between unlink and rename loses only that older slot, the newest copy stays.
 */
int refstore_save(const char *dir, int cam, enum ref_kind kind, const uint8_t *img, time_t taken)
{
    static uint8_t cur[IMGDIFF_W * IMGDIFF_H];
    uint32_t gen = 0;
    int have = -1;
    load_best(dir, cam, kind, cur, NULL, &gen, &have);
    int slot = have == 0 ? 1 : 0;

    char path[256], tmp[272];
    path_of(path, sizeof(path), dir, cam, kind, slot);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    struct ref_header h = {
        .magic = REF_MAGIC, .version = REF_VERSION,
        .width = IMGDIFF_W, .height = IMGDIFF_H,
        .payload_len = IMGDIFF_W * IMGDIFF_H,
        .taken = (int64_t)taken,
        .crc32 = refstore_crc32(img, IMGDIFF_W * IMGDIFF_H),
        .gen = have >= 0 ? gen + 1 : 1,
    };
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    int ok = write_all(fd, &h, sizeof(h)) == 0 && write_all(fd, img, h.payload_len) == 0 &&
             fsync(fd) == 0;
    close(fd);
    if (ok && unlink(path) != 0 && errno != ENOENT)
        ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);             /* make the rename itself durable */
        close(dfd);
    }
    return 0;
}
