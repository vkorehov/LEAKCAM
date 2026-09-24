#define _GNU_SOURCE
#include "refstore.h"
#include "imgdiff.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define REF_MAGIC   0x5453454Cu     /* "LEST" little-endian */
#define REF_VERSION 1u

struct ref_header {
    uint32_t magic, version;
    uint16_t width, height;
    uint32_t payload_len;
    int64_t  taken;
    uint32_t crc32;
    uint32_t reserved;
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

static void path_of(char *buf, size_t n, const char *dir, int cam, enum ref_kind kind)
{
    static const char *const ext[] = { [REF_LAST] = "last", [REF_BASE] = "base", [REF_HIST] = "hist" };
    snprintf(buf, n, "%s/cam%d.%s", dir, cam, ext[kind]);
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

int refstore_load(const char *dir, int cam, enum ref_kind kind, uint8_t *img, time_t *taken)
{
    char path[256];
    path_of(path, sizeof(path), dir, cam, kind);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? 1 : -1;
    struct ref_header h;
    int rc = 1;
    if (read_all(fd, &h, sizeof(h)) == 0 && h.magic == REF_MAGIC && h.version == REF_VERSION &&
        h.width == IMGDIFF_W && h.height == IMGDIFF_H && h.payload_len == IMGDIFF_W * IMGDIFF_H &&
        read_all(fd, img, h.payload_len) == 0 && refstore_crc32(img, h.payload_len) == h.crc32) {
        if (taken)
            *taken = (time_t)h.taken;
        rc = 0;
    } else {
        fprintf(stderr, "refstore: %s is damaged or from another version, ignoring it\n", path);
    }
    close(fd);
    return rc;
}

int refstore_save(const char *dir, int cam, enum ref_kind kind, const uint8_t *img, time_t taken)
{
    char path[256], tmp[272];
    path_of(path, sizeof(path), dir, cam, kind);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    struct ref_header h = {
        .magic = REF_MAGIC, .version = REF_VERSION,
        .width = IMGDIFF_W, .height = IMGDIFF_H,
        .payload_len = IMGDIFF_W * IMGDIFF_H,
        .taken = (int64_t)taken,
        .crc32 = refstore_crc32(img, IMGDIFF_W * IMGDIFF_H),
    };
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    int ok = write_all(fd, &h, sizeof(h)) == 0 && write_all(fd, img, h.payload_len) == 0 &&
             fsync(fd) == 0;
    close(fd);
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
