#define _GNU_SOURCE
#include "history.h"
#include "refstore.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define HIST_MAGIC   0x5453484Cu    /* "LHST" little-endian */
#define HIST_VERSION 1u
#define ZLEVEL       3              /* camera luminance barely gains from higher levels */

struct hist_header {
    uint32_t magic;
    uint16_t version;
    uint8_t  kind, cam;
    uint32_t seq, key_seq;
    int64_t  taken;
    uint16_t width, height;
    uint8_t  blocks_x, blocks_y;
    uint16_t nblocks;
    uint32_t raw_len, z_len;
    uint32_t crc32;                 /* over the block list and the compressed payload */
    uint32_t reserved;
};
_Static_assert(sizeof(struct hist_header) == 48, "header layout is part of the file format");

void hist_default_cfg(struct hist_cfg *cfg, const char *dir)
{
    cfg->dir = dir;
    cfg->quota_bytes = 32u << 20;   /* x 2 cameras: a quarter of the 256 MB W25N02KV */
    cfg->max_deltas = 96;           /* 16 h of changes at one wake per 10 min */
    cfg->key_frac = 0.5f;
}

/* ---- small file helpers ---- */

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

static void fsync_dir(const char *dir)
{
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
}

/* two buffers written as one file: tmp name, fsync, rename, fsync the directory */
static int write_atomic(const char *dir, const char *path, const void *a, size_t alen,
                        const void *b, size_t blen)
{
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    int ok = write_all(fd, a, alen) == 0 && (!blen || write_all(fd, b, blen) == 0) &&
             fsync(fd) == 0;
    int saved = errno;
    close(fd);
    if (!ok || rename(tmp, path) != 0) {
        saved = ok ? errno : saved;
        unlink(tmp);
        errno = saved;
        return -1;
    }
    fsync_dir(dir);
    return 0;
}

static void cam_dir(char *buf, size_t n, const char *dir, int cam)
{
    snprintf(buf, n, "%s/hist%d", dir, cam);
}

static void rec_path(char *buf, size_t n, const char *cdir, uint32_t seq, char kind)
{
    snprintf(buf, n, "%s/%08u.%c", cdir, seq, kind);
}

/* ---- directory scan: sequence, kind and size from the names, no file reads ---- */

struct rec { uint32_t seq; char kind; size_t bytes; };

static int by_seq(const void *a, const void *b)
{
    const struct rec *x = a, *y = b;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* sorted records of one camera directory; stale .tmp files from a power cut are removed */
static int scan(const char *cdir, struct rec **out, size_t *n)
{
    *out = NULL;
    *n = 0;
    DIR *d = opendir(cdir);
    if (!d)
        return errno == ENOENT ? 0 : -1;
    size_t cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        unsigned seq;
        char kind, extra;
        int len = (int)strlen(e->d_name);
        if (len > 4 && !strcmp(e->d_name + len - 4, ".tmp")) {
            unlinkat(dirfd(d), e->d_name, 0);
            continue;
        }
        if (sscanf(e->d_name, "%8u.%c%c", &seq, &kind, &extra) != 2 || (kind != 'K' && kind != 'D'))
            continue;
        struct stat st;
        if (fstatat(dirfd(d), e->d_name, &st, 0) != 0)
            continue;
        if (*n == cap) {
            cap = cap ? cap * 2 : 64;
            struct rec *r = realloc(*out, cap * sizeof(**out));
            if (!r) {
                closedir(d);
                free(*out);
                *out = NULL;
                return -1;
            }
            *out = r;
        }
        (*out)[(*n)++] = (struct rec){ seq, kind, (size_t)st.st_size };
    }
    closedir(d);
    qsort(*out, *n, sizeof(**out), by_seq);
    return 0;
}

/* CRC-32 of a followed by b, same polynomial as refstore_crc32() */
static uint32_t crc_two(const void *a, size_t alen, const void *b, size_t blen)
{
    uint32_t crc = ~refstore_crc32(a, alen);
    const uint8_t *p = b;
    for (size_t i = 0; i < blen; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---- reading one record ---- */

/* header of one record, validated; 0 ok, 1 damaged or missing */
static int read_header(const char *path, struct hist_header *h)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 1;
    int ok = read_all(fd, h, sizeof(*h)) == 0 && h->magic == HIST_MAGIC &&
             h->version == HIST_VERSION && (h->kind == 'K' || h->kind == 'D');
    close(fd);
    return ok ? 0 : 1;
}

/* whole record: header, block list (malloc'd, deltas only) and decompressed payload (malloc'd) */
static int read_record(const char *path, struct hist_header *h, uint16_t **idx, uint8_t **raw)
{
    *idx = NULL;
    *raw = NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 1;
    uint8_t *z = NULL;
    int rc = 1;
    if (read_all(fd, h, sizeof(*h)) != 0 || h->magic != HIST_MAGIC || h->version != HIST_VERSION)
        goto out;
    size_t ilen = (size_t)h->nblocks * sizeof(uint16_t);
    *idx = malloc(ilen ? ilen : 1);
    z = malloc(h->z_len ? h->z_len : 1);
    *raw = malloc(h->raw_len ? h->raw_len : 1);
    if (!*idx || !z || !*raw) {
        rc = -1;
        goto out;
    }
    if (read_all(fd, *idx, ilen) != 0 || read_all(fd, z, h->z_len) != 0)
        goto out;
    if (crc_two(*idx, ilen, z, h->z_len) != h->crc32)
        goto out;
    uLongf rlen = h->raw_len;
    if (uncompress(*raw, &rlen, z, h->z_len) != Z_OK || rlen != h->raw_len)
        goto out;
    rc = 0;
out:
    close(fd);
    free(z);
    if (rc) {
        free(*idx);
        free(*raw);
        *idx = NULL;
        *raw = NULL;
    }
    return rc;
}

/* ---- store ---- */

/* delete whole oldest groups (and orphan deltas) until the camera is under quota */
static unsigned prune(const char *cdir, struct rec *r, size_t n, size_t quota)
{
    size_t total = 0, first = 0;
    unsigned deleted = 0;
    for (size_t i = 0; i < n; i++)
        total += r[i].bytes;
    size_t last_key = n;
    for (size_t i = n; i-- > 0;)
        if (r[i].kind == 'K') {
            last_key = i;
            break;
        }
    while (total > quota && first < last_key) {
        size_t end = first + 1;                   /* group: this record up to the next keyframe */
        while (end < last_key && r[end].kind != 'K')
            end++;
        for (size_t i = first; i < end; i++) {
            char path[320];
            rec_path(path, sizeof(path), cdir, r[i].seq, r[i].kind);
            if (unlink(path) == 0) {
                total -= r[i].bytes;
                deleted++;
            }
        }
        first = end;
    }
    if (deleted)
        fsync_dir(cdir);
    return deleted;
}

int hist_store(const struct hist_cfg *cfg, int cam, time_t taken, const uint8_t *luma,
               unsigned w, unsigned h,
               const uint8_t changed[IMGDIFF_BLOCKS_Y][IMGDIFF_BLOCKS_X], int usable_blocks,
               int force_key, struct hist_info *out)
{
    memset(out, 0, sizeof(*out));
    if (w % IMGDIFF_BLOCKS_X || h % IMGDIFF_BLOCKS_Y || w > 65535 || h > 65535) {
        errno = EINVAL;
        return -1;
    }
    const unsigned bw = w / IMGDIFF_BLOCKS_X, bh = h / IMGDIFF_BLOCKS_Y;

    char cdir[300];
    cam_dir(cdir, sizeof(cdir), cfg->dir, cam);
    if (mkdir(cdir, 0755) < 0 && errno != EEXIST)
        return -1;
    struct rec *r;
    size_t n;
    if (scan(cdir, &r, &n) < 0)
        return -1;

    /* the current group: its keyframe and how many deltas follow it */
    uint32_t next_seq = n ? r[n - 1].seq + 1 : 1, key_seq = 0;
    unsigned deltas = 0;
    for (size_t i = n; i-- > 0;) {
        if (r[i].kind == 'K') {
            key_seq = r[i].seq;
            break;
        }
        deltas++;
    }
    if (key_seq) {                                /* a resolution change needs a new keyframe */
        char path[320];
        struct hist_header kh;
        rec_path(path, sizeof(path), cdir, key_seq, 'K');
        if (read_header(path, &kh) != 0 || kh.width != w || kh.height != h)
            key_seq = 0;
    }

    unsigned nchg = 0;
    uint16_t idx[IMGDIFF_BLOCKS_X * IMGDIFF_BLOCKS_Y];
    for (int by = 0; by < IMGDIFF_BLOCKS_Y; by++)
        for (int bx = 0; bx < IMGDIFF_BLOCKS_X; bx++)
            if (changed && changed[by][bx])
                idx[nchg++] = (uint16_t)(by * IMGDIFF_BLOCKS_X + bx);

    int key = force_key || !key_seq || !changed || deltas >= cfg->max_deltas ||
              (usable_blocks > 0 && nchg > cfg->key_frac * (float)usable_blocks);
    if (!key && nchg == 0) {
        free(r);
        out->kind = HIST_NONE;
        return 0;
    }

    /* raw payload: the whole frame, or the changed blocks one after another */
    size_t raw_len = key ? (size_t)w * h : (size_t)nchg * bw * bh;
    uint8_t *raw = malloc(raw_len);
    uLongf zlen = compressBound(raw_len);
    uint8_t *z = malloc(zlen);
    if (!raw || !z) {
        free(raw);
        free(z);
        free(r);
        errno = ENOMEM;
        return -1;
    }
    if (key) {
        memcpy(raw, luma, raw_len);
        nchg = 0;
    } else {
        uint8_t *p = raw;
        for (unsigned k = 0; k < nchg; k++) {
            unsigned bx = idx[k] % IMGDIFF_BLOCKS_X, by = idx[k] / IMGDIFF_BLOCKS_X;
            for (unsigned y = 0; y < bh; y++, p += bw)
                memcpy(p, luma + (size_t)(by * bh + y) * w + bx * bw, bw);
        }
    }
    int rc = -1;
    if (compress2(z, &zlen, raw, raw_len, ZLEVEL) != Z_OK) {
        errno = EIO;
        goto out;
    }

    /* header + block list in one buffer, the compressed payload after it */
    size_t ilen = (size_t)nchg * sizeof(uint16_t);
    uint8_t head[sizeof(struct hist_header) + sizeof(idx)];
    struct hist_header hh = {
        .magic = HIST_MAGIC, .version = HIST_VERSION,
        .kind = key ? 'K' : 'D', .cam = (uint8_t)cam,
        .seq = next_seq, .key_seq = key ? next_seq : key_seq,
        .taken = (int64_t)taken,
        .width = (uint16_t)w, .height = (uint16_t)h,
        .blocks_x = IMGDIFF_BLOCKS_X, .blocks_y = IMGDIFF_BLOCKS_Y,
        .nblocks = (uint16_t)nchg,
        .raw_len = (uint32_t)raw_len, .z_len = (uint32_t)zlen,
        .crc32 = crc_two(idx, ilen, z, zlen),
    };
    memcpy(head, &hh, sizeof(hh));
    memcpy(head + sizeof(hh), idx, ilen);

    char path[320];
    rec_path(path, sizeof(path), cdir, hh.seq, (char)hh.kind);
    if (write_atomic(cdir, path, head, sizeof(hh) + ilen, z, zlen) != 0)
        goto out;

    out->kind = key ? HIST_KEY : HIST_DELTA;
    out->seq = hh.seq;
    out->key_seq = hh.key_seq;
    out->nblocks = nchg;
    out->bytes = sizeof(hh) + ilen + zlen;

    /* quota, counting the record just written */
    struct rec *r2 = realloc(r, (n + 1) * sizeof(*r));
    if (r2) {
        r = r2;
        r[n++] = (struct rec){ hh.seq, (char)hh.kind, out->bytes };
        out->pruned = prune(cdir, r, n, cfg->quota_bytes);
    }
    rc = 0;
out:
    free(raw);
    free(z);
    free(r);
    return rc;
}

/* ---- list and rebuild ---- */

int hist_list(const char *dir, int cam, struct hist_entry **list, size_t *n)
{
    char cdir[300];
    cam_dir(cdir, sizeof(cdir), dir, cam);
    struct rec *r;
    size_t nr;
    *list = NULL;
    *n = 0;
    if (scan(cdir, &r, &nr) < 0)
        return -1;
    *list = calloc(nr ? nr : 1, sizeof(**list));
    if (!*list) {
        free(r);
        return -1;
    }
    for (size_t i = 0; i < nr; i++) {
        char path[320];
        struct hist_header h;
        rec_path(path, sizeof(path), cdir, r[i].seq, r[i].kind);
        if (read_header(path, &h) != 0 || h.seq != r[i].seq)
            continue;                             /* damaged: not listed */
        (*list)[(*n)++] = (struct hist_entry){
            .seq = h.seq, .key_seq = h.key_seq, .kind = (enum hist_kind)h.kind,
            .taken = (time_t)h.taken, .w = h.width, .h = h.height, .nblocks = h.nblocks,
            .bytes = r[i].bytes,
        };
    }
    free(r);
    return 0;
}

int hist_rebuild(const char *dir, int cam, uint32_t seq, uint8_t **luma, unsigned *w, unsigned *h,
                 time_t *taken)
{
    *luma = NULL;
    char cdir[300];
    cam_dir(cdir, sizeof(cdir), dir, cam);
    struct rec *r;
    size_t n;
    if (scan(cdir, &r, &n) < 0)
        return -1;
    size_t target = n;
    for (size_t i = 0; i < n; i++)
        if (seq ? r[i].seq == seq : i == n - 1)
            target = i;
    size_t key = target;
    while (key < n && r[key].kind != 'K')
        key = key ? key - 1 : n;                  /* walk back to the group's keyframe */
    if (target == n || key == n) {
        free(r);
        return 1;
    }

    int rc = 1;
    uint8_t *frame = NULL;
    struct hist_header kh = { 0 };
    for (size_t i = key; i <= target; i++) {
        char path[320];
        struct hist_header hh;
        uint16_t *idx;
        uint8_t *raw;
        rec_path(path, sizeof(path), cdir, r[i].seq, r[i].kind);
        int e = read_record(path, &hh, &idx, &raw);
        if (e) {
            rc = e;
            goto out;
        }
        int ok = 0;
        if (i == key) {
            ok = hh.kind == 'K' && hh.raw_len == (uint32_t)hh.width * hh.height;
            if (ok) {
                kh = hh;
                frame = raw;
                raw = NULL;
            }
        } else {
            unsigned bw = kh.width / kh.blocks_x, bh = kh.height / kh.blocks_y;
            ok = hh.kind == 'D' && hh.key_seq == kh.seq && hh.width == kh.width &&
                 hh.height == kh.height && hh.raw_len == (uint32_t)hh.nblocks * bw * bh;
            const uint8_t *p = raw;
            for (unsigned k = 0; ok && k < hh.nblocks; k++) {
                unsigned bx = idx[k] % kh.blocks_x, by = idx[k] / kh.blocks_x;
                if (by >= kh.blocks_y) {
                    ok = 0;
                    break;
                }
                for (unsigned y = 0; y < bh; y++, p += bw)
                    memcpy(frame + (size_t)(by * bh + y) * kh.width + bx * bw, p, bw);
            }
        }
        if (taken)
            *taken = (time_t)hh.taken;
        free(idx);
        free(raw);
        if (!ok)
            goto out;
    }
    *luma = frame;
    frame = NULL;
    *w = kh.width;
    *h = kh.height;
    rc = 0;
out:
    free(frame);
    free(r);
    return rc;
}

int hist_write_pgm(const char *path, const uint8_t *luma, unsigned w, unsigned h)
{
    char head[32], dir[300];
    int hl = snprintf(head, sizeof(head), "P5\n%u %u\n255\n", w, h);
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    else
        snprintf(dir, sizeof(dir), ".");
    return write_atomic(dir, path, head, (size_t)hl, luma, (size_t)w * h);
}
