#include "jpeg_hw.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k_type.h"
#include "k_vb_comm.h"
#include "k_venc_comm.h"
#include "k_video_comm.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"

#include "jpeg.h"

#define CHN       0                  /* VENC channel 0: the only encoder user of the product program */
#define STREAM_N  2                  /* stream buffers: one picture at a time, one spare */
#define TIMEOUT   2000               /* ms for one picture */

static int ready;                    /* 1: the channel runs for chn_w x chn_h; -1: it failed (no pictures) */
static unsigned chn_w, chn_h;
static int chn_q;
static k_u32 pool = VB_INVALID_POOLID;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   /* the pictures (media.c) and the focus preview (focus.c) */

static void close_chn(void);

static int open_chn(unsigned w, unsigned h, int quality)
{
    /* a stream buffer of a quarter of the luma plane (a native q85 picture is ~0.15 MB; half, as sample_venc, took
     * 2.5 MB of the media pool more, needed by the videos, 2026-11-12) */
    pool = (k_u32)kd_mpi_vb_create_pool_ex(VB_ALIGN_UP(w * h / 4, 4096), STREAM_N, VB_REMAP_MODE_NOCACHE);
    if (pool == VB_INVALID_POOLID) {
        fprintf(stderr, "jpeg_hw: stream pool failed (MMZ full?)\n");
        return -1;
    }
    if (kd_mpi_venc_attach_vb_pool(CHN, pool)) {
        fprintf(stderr, "jpeg_hw: attach pool failed\n");
        return -1;
    }
    k_venc_chn_attr a;
    memset(&a, 0, sizeof(a));
    a.venc_attr.type = K_PT_JPEG;
    a.venc_attr.pic_width = w;
    a.venc_attr.pic_height = h;
    a.rc_attr.rc_mode = K_VENC_RC_MODE_MJPEG_FIXQP;
    a.rc_attr.mjpeg_fixqp.src_frame_rate = 1;
    a.rc_attr.mjpeg_fixqp.dst_frame_rate = 1;
    a.rc_attr.mjpeg_fixqp.q_factor = (k_u32)quality;
    if (kd_mpi_venc_create_chn(CHN, &a)) {
        fprintf(stderr, "jpeg_hw: channel %ux%u q%d failed\n", w, h, quality);
        return -1;
    }
    if (kd_mpi_venc_start_chn(CHN)) {
        fprintf(stderr, "jpeg_hw: channel %ux%u q%d failed\n", w, h, quality);
        return -1;
    }
    chn_w = w;
    chn_h = h;
    chn_q = quality;
    return 1;
}

static size_t encode(const void *frame, time_t taken, uint8_t **out);

size_t jpeg_hw(const void *frame, unsigned width, unsigned height, int quality, time_t taken, uint8_t **out)
{
    *out = NULL;
    pthread_mutex_lock(&mu);
    if (ready > 0 && (width != chn_w || height != chn_h || quality != chn_q))
        close_chn();                         /* another size: the focus preview between pictures */
    if (!ready)
        ready = open_chn(width, height, quality);
    size_t n = ready > 0 ? encode(frame, taken, out) : 0;
    pthread_mutex_unlock(&mu);
    return n;
}

static size_t encode(const void *frame, time_t taken, uint8_t **out)
{
    if (kd_mpi_venc_send_frame(CHN, (k_video_frame_info *)frame, TIMEOUT)) {
        fprintf(stderr, "jpeg_hw: send_frame failed\n");
        return 0;
    }
    k_venc_chn_status st;
    k_venc_pack packs[4];
    k_venc_stream s;
    memset(&s, 0, sizeof(s));
    if (kd_mpi_venc_query_status(CHN, &st))
        return 0;
    s.pack_cnt = st.cur_packs > 0 && st.cur_packs <= 4 ? st.cur_packs : 1;
    s.pack = packs;
    if (kd_mpi_venc_get_stream(CHN, &s, TIMEOUT)) {
        fprintf(stderr, "jpeg_hw: no picture within %d ms\n", TIMEOUT);
        return 0;
    }
    size_t total = 0;
    for (k_u32 i = 0; i < s.pack_cnt; i++)
        total += s.pack[i].len;
    uint8_t exif[JPEG_EXIF_MAX];
    size_t ex = taken > 0 ? jpeg_exif(taken, exif, sizeof(exif)) : 0;
    uint8_t *buf = malloc(total + ex);
    size_t n = 0;
    bool ok = buf != NULL;
    for (k_u32 i = 0; ok && i < s.pack_cnt; i++) {
        k_u8 *p = kd_mpi_sys_mmap(s.pack[i].phys_addr, s.pack[i].len);
        if (!p) {
            ok = false;
            break;
        }
        if (n == 0 && s.pack[i].len >= 2 && p[0] == 0xFF && p[1] == 0xD8 && ex) {
            memcpy(buf, p, 2);                   /* SOI, then the EXIF segment, then the encoder's own */
            memcpy(buf + 2, exif, ex);
            memcpy(buf + 2 + ex, p + 2, s.pack[i].len - 2);
            n = s.pack[i].len + ex;
        } else {
            memcpy(buf + n, p, s.pack[i].len);
            n += s.pack[i].len;
        }
        kd_mpi_sys_munmap(p, s.pack[i].len);
    }
    kd_mpi_venc_release_stream(CHN, &s);
    if (!ok || n < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        free(buf);
        return 0;
    }
    *out = buf;
    return n;
}

void jpeg_hw_close(void)
{
    pthread_mutex_lock(&mu);
    close_chn();
    pthread_mutex_unlock(&mu);
}

static void close_chn(void)
{
    if (ready > 0) {
        kd_mpi_venc_stop_chn(CHN);
        kd_mpi_venc_detach_vb_pool(CHN);
        kd_mpi_venc_destroy_chn(CHN);
    }
    if (pool != VB_INVALID_POOLID)
        kd_mpi_vb_destory_pool(pool);
    pool = VB_INVALID_POOLID;
    ready = 0;
}
