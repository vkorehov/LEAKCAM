#include "h265.h"

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

#define CAMS      2
#define STREAM_N  2                     /* stream buffers per channel (the encoder writes one while we read one) */
#define TIMEOUT   1000                  /* ms for one frame (it takes ~10 at 1280x960, test027) */
#define PACKS     8

static k_u32 pool[CAMS] = { VB_INVALID_POOLID, VB_INVALID_POOLID };
static bool open_[CAMS];

int h265_open(int cam, unsigned w, unsigned h, unsigned fps)
{
    if (cam < 0 || cam >= CAMS || open_[cam])
        return -1;
    int chn = 1 + cam;
    /* a stream buffer of a quarter of the luma plane (1.25 MB native), two: a sharp camera's IDR at 10 Mbit/s did not
     * fit an eighth (627 KB: a cut frame, "initial delimiter not found" at the server, test147 2026-11-12); the media
     * pool has ~7 MB left beside the cameras' packed raw buffers (/proc/media-mem 67.5 of 75 MB) */
    pool[cam] = (k_u32)kd_mpi_vb_create_pool_ex(VB_ALIGN_UP(w * h / 4, 4096), STREAM_N, VB_REMAP_MODE_NOCACHE);
    if (pool[cam] == VB_INVALID_POOLID) {
        fprintf(stderr, "h265: cam %d stream pool failed (MMZ full?)\n", cam);
        return -1;
    }
    k_venc_chn_attr a;
    memset(&a, 0, sizeof(a));
    a.venc_attr.type = K_PT_H265;
    a.venc_attr.pic_width = w;
    a.venc_attr.pic_height = h;
    a.venc_attr.profile = VENC_PROFILE_H265_MAIN;
    a.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
    a.rc_attr.cbr.gop = fps;                         /* an IDR every second */
    a.rc_attr.cbr.src_frame_rate = fps;
    a.rc_attr.cbr.dst_frame_rate = fps;
    a.rc_attr.cbr.bit_rate = H265_KBPS;
    /* IDR frames enabled as every SDK sample does after the create (sample_webrtc mpp_pipeline.c): without it
     * kd_mpi_venc_request_idr was ignored, the IDRs came only with the GOP (the focus switch, test146 2026-11-13) */
    if (kd_mpi_venc_attach_vb_pool(chn, pool[cam]) || kd_mpi_venc_create_chn(chn, &a) ||
        kd_mpi_venc_enable_idr(chn, K_TRUE) || kd_mpi_venc_start_chn(chn)) {
        fprintf(stderr, "h265: cam %d channel %ux%u@%u failed\n", cam, w, h, fps);
        kd_mpi_venc_detach_vb_pool(chn);
        kd_mpi_vb_destory_pool(pool[cam]);
        pool[cam] = VB_INVALID_POOLID;
        return -1;
    }
    open_[cam] = true;
    return 0;
}

/* one fetch from the encoder appended to *buf (*n bytes so far); true when it held a picture (not only the
 * VPS/SPS/PPS header the first frame's fetch can bring alone: the picture then is in the next) */
static bool fetch(int cam, uint8_t **buf, size_t *n, int *idr);

size_t h265_frame(int cam, const void *frame, uint8_t **out, int *idr)
{
    *out = NULL;
    *idr = 0;
    if (cam < 0 || cam >= CAMS || !open_[cam])
        return 0;
    int chn = 1 + cam;
    if (kd_mpi_venc_send_frame(chn, (k_video_frame_info *)frame, TIMEOUT)) {
        fprintf(stderr, "h265: cam %d send_frame failed\n", cam);
        return 0;
    }
    uint8_t *buf = NULL;
    size_t n = 0;
    for (int k = 0; k < 2 && *out == NULL; k++)
        if (fetch(cam, &buf, &n, idr) || !buf)
            break;
    *out = buf;
    return buf ? n : 0;
}

static bool fetch(int cam, uint8_t **bufp, size_t *np, int *idr)
{
    int chn = 1 + cam;
    k_venc_chn_status st;
    k_venc_pack packs[PACKS];
    k_venc_stream s;
    memset(&s, 0, sizeof(s));
    if (kd_mpi_venc_query_status(chn, &st))
        return true;
    s.pack_cnt = st.cur_packs > 0 && st.cur_packs <= PACKS ? st.cur_packs : 1;
    s.pack = packs;
    if (kd_mpi_venc_get_stream(chn, &s, TIMEOUT)) {
        fprintf(stderr, "h265: cam %d no frame within %d ms\n", cam, TIMEOUT);
        return true;                                 /* nothing more to wait for */
    }
    size_t total = *np;
    for (k_u32 i = 0; i < s.pack_cnt; i++)
        total += s.pack[i].len;
    uint8_t *buf = realloc(*bufp, total);
    bool picture = false;
    for (k_u32 i = 0; buf && i < s.pack_cnt; i++) {
        k_u8 *p = kd_mpi_sys_mmap(s.pack[i].phys_addr, s.pack[i].len);
        if (!p) {
            free(buf);
            buf = NULL;
            break;
        }
        memcpy(buf + *np, p, s.pack[i].len);         /* the encoder's packs are Annex-B (start codes in) */
        *np += s.pack[i].len;
        kd_mpi_sys_munmap(p, s.pack[i].len);
        if (s.pack[i].type == K_VENC_I_FRAME)
            *idr = 1;
        if (s.pack[i].type != K_VENC_HEADER)
            picture = true;
    }
    kd_mpi_venc_release_stream(chn, &s);
    if (!buf)
        free(*bufp);
    *bufp = buf;
    return picture;
}

void h265_idr(int cam)
{
    if (cam >= 0 && cam < CAMS && open_[cam] && kd_mpi_venc_request_idr(1 + cam))
        fprintf(stderr, "h265: cam %d IDR request failed\n", cam);
}

void h265_close(int cam)
{
    if (cam < 0 || cam >= CAMS || !open_[cam])
        return;
    int chn = 1 + cam;
    kd_mpi_venc_stop_chn(chn);
    kd_mpi_venc_detach_vb_pool(chn);
    kd_mpi_venc_destroy_chn(chn);
    kd_mpi_vb_destory_pool(pool[cam]);
    pool[cam] = VB_INVALID_POOLID;
    open_[cam] = false;
}
