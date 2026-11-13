/*
 * RT-Smart camera backend: k230_sdk MPP VICAP, both OV5647 at their native 2592x1944 (15 fps), one NV12 frame each,
 * held in the video buffer and handed to the caller mapped (no copy). The product job's capture (job.c); the 1280x960
 * binned mode before 2026-11-12 was proven on the bench by tests/test030 (cam_shot).
 *
 * Call order and buffer sizing follow src/big/mpp/userapps/sample/sample_vicap/sample_vicap.c:
 *   get_sensor_info, set_dev_attr (every device) -> VB pools -> set_dump_reserved, set_chn_attr
 *   -> init (every device) -> start_stream (every device) -> dump_frame / mmap / dump_release
 *   -> stop_stream, deinit -> vb_exit.
 * Two sensors need VICAP_WORK_OFFLINE_MODE (online mode takes one sensor only): each device writes
 * raw frames to DDR and the ISP processes them in turn.
 *
 * The sensor's I2C bus, reset pin and MCLK come from the board block in k_board_config_comm.h
 * (OV5647_IIC / OV5647_CAM_PIN ...), not from this file: LEAKCAM needs CSI0 on "i2c0" and CSI2
 * on "i2c4". See rtsmart/README.md.
 */
#include "cap.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k_type.h"
#include "k_vb_comm.h"
#include "k_vicap_comm.h"
#include "k_video_comm.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_vicap_api.h"

/* native frames: raw 2592x1944x2 = 10.1 MB, NV12 7.6 MB; two cameras with 2 + 2 = ~71 MB of the 80 MB media pool
 * (k230d_rtos_leakcam_defconfig CONFIG_MEM_MMZ_SIZE; the sample's 3 + 3 = 106 MB does not fit 128 MB DDR) */
#define IN_BUF_NUM  3              /* raw input buffers per device (offline mode): 2 dropped every other frame
                                    * (7.5 fps), 3 give the native 15 (packed: rawpack.c, VIDEO.txt) */
#define OUT_BUF_NUM 2              /* NV12 output buffers per channel */

/* slot -> sensor mode; both the native 2592x1944@15 mode of the same register table.
 * k230_rtos_sdk / CanMV MPP (default) names them without the OV_ prefix and only with
 * CONFIG_MPP_ENABLE_SENSOR_OV5647; the older k230_sdk MPP has fixed OV_ values 45 and 48. */
static const k_vicap_sensor_type slot_sensor[2] = {
    OV5647_MIPI_CSI0_2592x1944_15FPS_10BIT_LINEAR,     /* CAM2, J4, CSI0: native (user 2026-10-21, built 2026-11-12) */
    OV5647_MIPI_CSI2_2592x1944_15FPS_10BIT_LINEAR,     /* CAM1, J5, CSI2 */
};

static int vb_ready, dev_started[CAP_MAX_CAMS], dev_inited[CAP_MAX_CAMS];
static k_video_frame_info held[CAP_MAX_CAMS];      /* each camera's kept frame, until cap_release_all() */
static k_vicap_chn held_chn;                       /* their channel: 0 native, 1 the 320x240 one */
static k_u64 probe_pts[CAP_MAX_CAMS], period[CAP_MAX_CAMS];   /* cap_ambient's frame and the frame period (pts) */
static float cur_exp[CAP_MAX_CAMS];                /* the exposure the sensor has now (set_exposure) */
static const struct cap_cam *first;                /* cams[0] of the open set: set_exposure's index */
static void stop_all(struct cap_cam *cams, int n);
unsigned cap_curve;


/* exposure time x gain (s) to the sensor: the longest time first (less noise), the rest as analog gain; clamped to the
 * mode's range. Kept in c->exp as set */
static bool set_exposure(struct cap_cam *c, float e)
{
    float lo = CAP_T_MIN * CAP_G_MIN, hi = CAP_T_MAX * CAP_G_MAX;
    e = e < lo ? lo : e > hi ? hi : e;
    float t = e < CAP_T_MAX ? e : CAP_T_MAX;
    float g = e / t;
    if (t < CAP_T_MIN)
        t = CAP_T_MIN;
    if (g < CAP_G_MIN)
        g = CAP_G_MIN;
    k_sensor_intg_time it;
    k_sensor_gain ga;
    memset(&it, 0, sizeof(it));
    memset(&ga, 0, sizeof(ga));
    it.intg_time[0] = t;
    ga.gain[0] = g;
    bool changed = fabsf(t * g - c->exp) > 1e-6f;
    if (changed && (kd_mpi_vicap_intg_time_set((k_vicap_dev)c->node, it) ||
                    kd_mpi_vicap_again_set((k_vicap_dev)c->node, ga)))
        fprintf(stderr, "vicap: dev %u exposure %.4f s x %.2f not set\n", c->node, t, g);
    c->exp = t * g;
    cur_exp[c - first] = c->exp;
    return changed;
}

int cap_open_all(struct cap_cam *cams, int n, unsigned width, unsigned height)
{
    k_vb_config vb;
    memset(&vb, 0, sizeof(vb));
    vb.max_pool_cnt = 64;                         /* VICAP creates its own pools (below) */
    k_u32 out_size[CAP_MAX_CAMS] = { 0 };

    for (int i = 0; i < n; i++) {
        struct cap_cam *c = &cams[i];
        c->slot = c->node;
        if (c->node > 1) {
            fprintf(stderr, "vicap: camera %u does not exist (0 = CSI0/J4, 1 = CSI2/J5)\n", c->node);
            return -1;
        }
        k_vicap_dev_attr dev;
        memset(&dev, 0, sizeof(dev));
        if (kd_mpi_vicap_get_sensor_info(slot_sensor[c->node], &dev.sensor_info)) {
            fprintf(stderr, "vicap: sensor type %d not in this build\n", slot_sensor[c->node]);
            return -1;
        }
        if (width != dev.sensor_info.width || height != dev.sensor_info.height)
            fprintf(stderr, "vicap: %ux%u requested, the sensor mode gives %ux%u\n", width, height,
                    dev.sensor_info.width, dev.sensor_info.height);
        c->width = dev.sensor_info.width;
        c->height = dev.sensor_info.height;
        c->stride = c->width;

        dev.input_type = VICAP_INPUT_TYPE_SENSOR;
        dev.acq_win.width = c->width;
        dev.acq_win.height = c->height;
        dev.mode = VICAP_WORK_OFFLINE_MODE;
        dev.buffer_num = IN_BUF_NUM;
        /* the raw frames 10-bit packed, rows padded to 16 B (rawpack.c: the ISP library's own size check is told
         * the packed size too) */
        dev.buffer_size = VICAP_ALIGN_UP(((c->width * 10 / 8 + 15) & ~15u) * c->height, VICAP_ALIGN_1K);
        dev.pipe_ctrl.data = 0xFFFFFFFF;          /* the sample's default: every ISP block on */
        dev.pipe_ctrl.bits.af_enable = 0;
        dev.pipe_ctrl.bits.ahdr_enable = 0;
        dev.pipe_ctrl.bits.dnr3_enable = 0;       /* 3DNR needs extra reference buffers */
        dev.pipe_ctrl.bits.ae_enable = 0;        /* exposure is ours (set_exposure, cap.h): the ISP's AE took 14-40
                                                   * frames per shot with the LEDs on (2026-11-12) */
        dev.pipe_ctrl.bits.awb_enable = 1;
        dev.cpature_frame = 0;                    /* continuous; we stop the stream ourselves */
        dev.dw_enable = K_FALSE;
        /* 0 is a real pool id: without this both devices would share one 3-block pool */
        dev.buffer_pool_id = VB_INVALID_POOLID;
        if (kd_mpi_vicap_set_dev_attr((k_vicap_dev)c->node, dev)) {
            fprintf(stderr, "vicap: dev %u set_dev_attr failed\n", c->node);
            return -1;
        }

        out_size[i] = VICAP_ALIGN_UP(c->width * c->height * 3 / 2, VICAP_ALIGN_1K);
    }
    if (kd_mpi_vb_set_config(&vb) || kd_mpi_vb_init()) {
        fprintf(stderr, "vicap: video buffer pools failed (MMZ too small?)\n");
        return -1;
    }
    vb_ready = 1;

    for (int i = 0; i < n; i++) {
        struct cap_cam *c = &cams[i];
        k_vicap_chn_attr chn;
        memset(&chn, 0, sizeof(chn));
        /* the picture 16-aligned: 2592x1944 cropped to the centred 2592x1936 (8 rows, 0.4 %); the JPEG encoder's 4:2:0
         * blocks are 16 rows and at 1944 it answered "V5/V7-JPEGENC: mve_mem_resize", no picture (2026-11-12) */
        unsigned full_h = c->height;
        c->height &= ~15u;
        chn.out_win.width = c->width;
        chn.out_win.height = c->height;
        chn.crop_win = chn.out_win;
        chn.crop_win.v_start = (k_u16)((full_h - c->height) / 2);
        chn.scale_win = chn.out_win;
        chn.crop_enable = c->height != full_h ? K_TRUE : K_FALSE;
        chn.scale_enable = K_FALSE;
        chn.chn_enable = K_TRUE;
        chn.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        chn.buffer_num = OUT_BUF_NUM;
        chn.buffer_size = out_size[i];
        chn.fps = 0;                              /* sensor rate */
        chn.buffer_pool_id = VB_INVALID_POOLID;
        chn.alignment = 12;                       /* 4 KB-aligned frames: the JPEG encoder takes them as they are
                                                   * (sample_venc; without it no picture came out, 2026-11-12) */
        kd_mpi_vicap_set_dump_reserved((k_vicap_dev)c->node, VICAP_CHN_ID_0, K_TRUE);
        if (kd_mpi_vicap_set_chn_attr((k_vicap_dev)c->node, VICAP_CHN_ID_0, chn)) {
            fprintf(stderr, "vicap: dev %u set_chn_attr failed\n", c->node);
            return -1;
        }
        /* channel 1: the same picture scaled by the ISP to 320x240, for the light probe and the exposure only (the
         * leak check compares the native frame: user 2026-11-12, "low resolution will not see any leak") */
        k_vicap_chn_attr small = chn;
        small.out_win.width = CAP_SMALL_W;
        small.out_win.height = CAP_SMALL_H;
        small.out_win.h_start = small.out_win.v_start = 0;
        small.scale_win = small.out_win;
        small.buffer_num = 3;
        small.buffer_size = VICAP_ALIGN_UP(CAP_SMALL_W * CAP_SMALL_H * 3 / 2, 4096);
        kd_mpi_vicap_set_dump_reserved((k_vicap_dev)c->node, VICAP_CHN_ID_1, K_TRUE);
        if (kd_mpi_vicap_set_chn_attr((k_vicap_dev)c->node, VICAP_CHN_ID_1, small)) {
            fprintf(stderr, "vicap: dev %u set_chn_attr (320x240) failed\n", c->node);
            return -1;
        }
    }
    /* init + start once: the stream runs for as long as the cameras are open, a grab only takes frames from it. The
     * closed VICAP/ISP library cannot restart a stream in a running program (2026-11-12, every way tried):
     *   - stop_stream, then start_stream again: "VsiCamDeviceGetPathStreaming stream failed(12)" in the ISP;
     *   - stop_stream + deinit, then init again: "kd_mpi_vicap_init, sensor open failed";
     *   - a full close (deinit + vb exit) and open per check: works, but leaks ~1.6 kernel threads and ~240 KB kernel
     *     heap per cycle (22 -> 54 threads, 6.5 -> 1.7 MB free in 20 checks, then the job hung).
     * The SDK's own users start once and stop only at the end (kdmedia/media.cpp, sample_uvc_dev_vicap). A missing
     * camera fails here (its sensor does not answer on its I2C bus): job.c opens the others */
    for (int i = 0; i < n; i++) {
        if (kd_mpi_vicap_init((k_vicap_dev)cams[i].node)) {
            fprintf(stderr, "vicap: dev %u init failed (sensor not answering on its I2C bus?)\n",
                    cams[i].node);
            return -1;
        }
        dev_inited[i] = 1;
    }
    for (int i = 0; i < n; i++) {
        if (kd_mpi_vicap_start_stream((k_vicap_dev)cams[i].node)) {
            fprintf(stderr, "vicap: dev %u start_stream failed\n", cams[i].node);
            return -1;
        }
        dev_started[i] = 1;
        first = cams;
        float e = cams[i].exp > 0 ? cams[i].exp : CAP_EXP_DEFAULT;   /* the caller's: where the last session ended */
        cams[i].exp = -1;                         /* unknown to the sensor: set */
        set_exposure(&cams[i], e);
    }
    rawpack_writers();                            /* the writers' line lengths packed (rawpack.c) */
    /* the sensor's first ~14 frames are black whatever the light: dropped here, LEDs off, so the first shot after a
     * cold start is as quick as any (2026-11-12: 14 black frames, then the ramp) */
    for (unsigned f = 0; f < CAP_WARMUP; f++)
        for (int i = 0; i < n; i++) {
            k_video_frame_info v;
            memset(&v, 0, sizeof(v));
            if (kd_mpi_vicap_dump_frame((k_vicap_dev)cams[i].node, VICAP_CHN_ID_1, VICAP_DUMP_YUV, &v, 1000)) {
                fprintf(stderr, "vicap: dev %u no frame at the warm-up\n", cams[i].node);
                return -1;
            }
            kd_mpi_vicap_dump_release((k_vicap_dev)cams[i].node, VICAP_CHN_ID_1, &v);
        }
    rawpack_writers();                            /* again after the library's whole configuration */
    return 0;
}

static void stop_all(struct cap_cam *cams, int n)
{
    for (int i = 0; i < n; i++) {
        if (dev_started[i] && kd_mpi_vicap_stop_stream((k_vicap_dev)cams[i].node))
            fprintf(stderr, "vicap: dev %u stop_stream failed\n", cams[i].node);
        if (dev_inited[i] && kd_mpi_vicap_deinit((k_vicap_dev)cams[i].node))
            fprintf(stderr, "vicap: dev %u deinit failed\n", cams[i].node);
        dev_started[i] = dev_inited[i] = 0;
    }
}

/* the mean of the sparse luma sample (every 16th pixel of every 16th row) */
static unsigned frame_mean(const k_video_frame_info *f, const k_u8 *y, unsigned stride)
{
    unsigned step = f->v_frame.width / 160 ? f->v_frame.width / 160 : 1;   /* ~160 x 120 samples at any size */
    unsigned long sum = 0, cnt = 0;
    for (unsigned r = step / 2; r < f->v_frame.height; r += step)
        for (unsigned x = step / 2; x < f->v_frame.width; x += step, cnt++)
            sum += y[(size_t)r * stride + x];
    return cnt ? (unsigned)(sum / cnt) : 0;
}

int cap_ambient(struct cap_cam *cams, int n, int timeout_ms)
{
    for (int i = 0; i < n; i++) {
        struct cap_cam *c = &cams[i];
        k_video_frame_info f;
        /* the second frame: the one in flight may have been exposed with the last shot's LEDs still on (it read the
         * room as bright, the next shot ran unlit, 2026-11-12) */
        /* the queue first: unread, its buffers hold frames seconds old (the first check after a pause compared a
         * stale probe, then kept a part-lit frame, test137 2026-11-12); then two fresh frames: the probe and the period */
        for (int k = 0; k < 8; k++) {
            memset(&f, 0, sizeof(f));
            if (kd_mpi_vicap_dump_frame((k_vicap_dev)c->node, VICAP_CHN_ID_1, VICAP_DUMP_YUV, &f, 0))
                break;
            kd_mpi_vicap_dump_release((k_vicap_dev)c->node, VICAP_CHN_ID_1, &f);
        }
        k_u64 first_pts = 0;
        for (int k = 0; k < 2; k++) {
            memset(&f, 0, sizeof(f));
            if (kd_mpi_vicap_dump_frame((k_vicap_dev)c->node, VICAP_CHN_ID_1, VICAP_DUMP_YUV, &f, (k_u32)timeout_ms))
                return -1;
            if (k == 0) {
                first_pts = f.v_frame.pts;
                kd_mpi_vicap_dump_release((k_vicap_dev)c->node, VICAP_CHN_ID_1, &f);
            }
        }
        probe_pts[i] = f.v_frame.pts;                      /* the last frame with the LEDs off */
        if (f.v_frame.pts > first_pts)
            period[i] = f.v_frame.pts - first_pts;
        unsigned stride = f.v_frame.stride[0] ? f.v_frame.stride[0] : f.v_frame.width;
        k_u32 ysize = stride * f.v_frame.height;
        const k_u8 *y = kd_mpi_sys_mmap(f.v_frame.phys_addr[0], ysize);
        if (y) {
            c->ambient = frame_mean(&f, y, stride);
            c->ambient_exp = c->exp;
            kd_mpi_sys_munmap((void *)y, ysize);
        }
        kd_mpi_vicap_dump_release((k_vicap_dev)c->node, VICAP_CHN_ID_1, &f);
        if (!y)
            return -1;
    }
    return 0;
}

int cap_grab_all(struct cap_cam *cams, int n, int small, unsigned settle, int timeout_ms)
{
    k_vicap_chn ch = small ? VICAP_CHN_ID_1 : VICAP_CHN_ID_0;
    held_chn = ch;
    for (int i = 0; i < n; i++)
        cams[i].frames = cams[i].kept = 0;
    /* Round-robin over the cameras: both stream at once. A camera's frames after its exposure was set: CAP_APPLY
     * dropped (the LEDs and the setting land), the next one measured (a sparse sample of the luma); in the band: kept,
     * else the exposure corrected and measured again. dump_frame blocks until that device's next frame. */
    int done = 0;
    unsigned wait[CAP_MAX_CAMS];
    unsigned limit[CAP_MAX_CAMS] = { 0 };            /* times a camera was found at its exposure limit, off the band */
    for (int i = 0; i < n; i++) {
        /* the exposure as set (the stream keeps the last one): only the LEDs change, and the frame after the one
         * exposing at the switch is fully lit (2026-11-12 native: 78, then 95 95 95...); a new exposure lands
         * CAP_APPLY frames later */
        float want = cams[i].exp > 0 ? cams[i].exp : CAP_EXP_DEFAULT;
        cams[i].exp = cur_exp[i];
        wait[i] = set_exposure(&cams[i], want) ? CAP_APPLY : 0;   /* the LEDs: the pts rule below */
        cams[i].tries = 1;
    }
    while (done < n) {
        for (int i = 0; i < n; i++) {
            struct cap_cam *c = &cams[i];
            if (c->frames && c->kept)
                continue;
            k_video_frame_info f;
            memset(&f, 0, sizeof(f));
            if (kd_mpi_vicap_dump_frame((k_vicap_dev)c->node, ch, VICAP_DUMP_YUV, &f,
                                        (k_u32)timeout_ms)) {
                fprintf(stderr, "vicap: dev %u no frame within %d ms, %d of %d cameras delivered\n",
                        c->node, timeout_ms, done, n);
                return -1;
            }
            c->frames++;
            unsigned stride = f.v_frame.stride[0] ? f.v_frame.stride[0] : f.v_frame.width;
            k_u32 ysize = stride * f.v_frame.height;
            const k_u8 *y = kd_mpi_sys_mmap(f.v_frame.phys_addr[0], ysize);
            if (!y) {
                kd_mpi_vicap_dump_release((k_vicap_dev)c->node, ch, &f);
                fprintf(stderr, "vicap: dev %u mmap of the frame failed\n", c->node);
                return -1;
            }
            unsigned m = frame_mean(&f, y, stride);
            c->mean = m;
            if (c->frames <= sizeof(c->trace))
                c->trace[c->frames - 1] = (uint8_t)m;
            bool keep = false;
            /* exposed at least 1.5 frame periods after the probe (LEDs off): the frame overlapping the switch, and any
             * older one still queued (channel 1 queues 3), is not the lit scene (a part-lit one passed the brightness
             * band: "changed" against the history, test137 2026-11-12) */
            if (probe_pts[i] && period[i] &&
                f.v_frame.pts < probe_pts[i] + period[i] * 3 / 2 + (k_u64)period[i] * cap_light_frames) {
                kd_mpi_sys_munmap((void *)y, ysize);
                kd_mpi_vicap_dump_release((k_vicap_dev)c->node, ch, &f);
                continue;
            }
            if (wait[i]) {
                wait[i]--;                               /* the change not in this frame yet */
            } else if (cap_curve) {
                keep = c->frames > settle;
            } else if ((m >= CAP_TARGET * CAP_LOW && m <= CAP_TARGET * CAP_HIGH) || c->frames > settle) {
                keep = true;
            } else {
                /* the ratio squared (gamma ~2), at most x8 / 1/8 per step; a black frame: x8 */
                float r = m < CAP_BLACK ? 8.0f : powf((float)CAP_TARGET / (float)m, 2.0f);
                r = r > 8.0f ? 8.0f : r < 0.125f ? 0.125f : r;
                float before = c->exp;
                set_exposure(c, c->exp * r);
                if (c->exp == before && limit[i]++) {
                    keep = true;                         /* at the limit, twice: the best this light gives */
                } else if (c->exp == before) {
                    /* at the limit and still off the band: measured once more CAP_APPLY frames on. The LEDs' light
                     * may not be in this frame yet (2026-11-13, a dark room, the stored exposure at its end: the
                     * frame kept at once read 15, the same scene 105 with the LEDs on longer; every later shot
                     * started at the limit and kept its first frame: dark pictures, "dark" checks) */
                    wait[i] = CAP_APPLY;
                } else {
                    c->tries++;
                    wait[i] = CAP_APPLY;
                }
            }
            kd_mpi_sys_munmap((void *)y, ysize);
            if (keep || c->frames > settle) {
                c->kept = 1;
                held[i] = f;               /* held until cap_release_all(): every camera's at one moment */
                done++;
                continue;
            }
            kd_mpi_vicap_dump_release((k_vicap_dev)c->node, ch, &f);
        }
    }
    return 0;
}

int cap_release_all(struct cap_cam *cams, int n, cap_frame_fn fn, void *ctx)
{
    int rc = 0;
    for (int i = 0; i < n; i++) {
        struct cap_cam *c = &cams[i];
        if (!c->kept)
            continue;
        k_video_frame_info *f = &held[i];
        unsigned stride = f->v_frame.stride[0] ? f->v_frame.stride[0] : f->v_frame.width;
        k_u32 ysize = stride * f->v_frame.height, uvsize = ysize / 2;
        k_u64 uv_phys = f->v_frame.phys_addr[1] ? f->v_frame.phys_addr[1] : f->v_frame.phys_addr[0] + ysize;
        /* the luma cached (the leak check reads all 5 MB: uncached that took ~1 s), fresh from the ISP's writes */
        k_u8 *y = fn ? kd_mpi_sys_mmap_cached(f->v_frame.phys_addr[0], ysize) : NULL;
        if (y)
            kd_mpi_sys_mmz_invalidate_cache(f->v_frame.phys_addr[0], y, ysize);
        const k_u8 *uv = fn ? kd_mpi_sys_mmap(uv_phys, uvsize) : NULL;
        c->fw = f->v_frame.width;
        c->fh = f->v_frame.height;
        if (fn && y && uv)
            fn(c, y, uv, stride, f, ctx);
        else if (fn) {
            fprintf(stderr, "vicap: dev %u mmap of the held frame failed\n", c->node);
            rc = -1;
        }
        if (y)
            kd_mpi_sys_munmap((void *)y, ysize);
        if (uv)
            kd_mpi_sys_munmap((void *)uv, uvsize);
        kd_mpi_vicap_dump_release((k_vicap_dev)c->node, held_chn, f);
        c->kept = 0;
    }
    return rc;
}

int cap_stream(struct cap_cam *cams, int n, unsigned frames, unsigned max_ms, unsigned map, cap_frame_fn fn, void *ctx,
               int timeout_ms)
{
    k_u64 first[CAP_MAX_CAMS] = { 0 }, last[CAP_MAX_CAMS] = { 0 };
    k_u32 ref0[CAP_MAX_CAMS] = { 0 }, ref1[CAP_MAX_CAMS] = { 0 };   /* the frames' sequence numbers: a step of 2
                                                                    * between the ones we get = one dropped */
    unsigned k;
    for (k = 0; k < frames && !(first[0] && last[0] - first[0] >= (k_u64)max_ms * 1000); k++)
        for (int i = 0; i < n; i++) {
            struct cap_cam *c = &cams[i];
            k_video_frame_info f;
            memset(&f, 0, sizeof(f));
            if (kd_mpi_vicap_dump_frame((k_vicap_dev)c->node, VICAP_CHN_ID_0, VICAP_DUMP_YUV, &f, (k_u32)timeout_ms)) {
                fprintf(stderr, "vicap: dev %u no video frame within %d ms (frame %u)\n", c->node, timeout_ms, k);
                return -1;
            }
            c->fw = f.v_frame.width;
            c->fh = f.v_frame.height;
            if (!first[i]) {
                first[i] = f.v_frame.pts;
                ref0[i] = f.v_frame.time_ref;
            }
            last[i] = f.v_frame.pts;
            ref1[i] = f.v_frame.time_ref;
            unsigned stride = f.v_frame.stride[0] ? f.v_frame.stride[0] : f.v_frame.width;
            k_u32 ysize = stride * f.v_frame.height;
            bool mapped = (map >> i) & 1u;
            k_u8 *y = mapped ? kd_mpi_sys_mmap_cached(f.v_frame.phys_addr[0], ysize) : NULL;
            if (y) {                               /* the luma cached and fresh, as cap_release_all's */
                kd_mpi_sys_mmz_invalidate_cache(f.v_frame.phys_addr[0], y, ysize);
                c->mean = frame_mean(&f, y, stride);   /* for cap_follow(): the sparse sample, ~10 k pixels */
            }
            if (!mapped || y)
                fn(c, y, NULL, stride, &f, ctx);
            else
                fprintf(stderr, "vicap: dev %u mmap of a video frame failed\n", c->node);
            if (y)
                kd_mpi_sys_munmap((void *)y, ysize);
            kd_mpi_vicap_dump_release((k_vicap_dev)c->node, VICAP_CHN_ID_0, &f);
        }
    for (int i = 0; i < n && k > 1; i++)           /* the frames' own times (us): the sensor's rate, not the loop's */
        fprintf(stderr, "vicap: dev %u video: %u frames over %llu ms of pts = %.1f fps, sequence %u..%u\n",
                cams[i].node, k, (unsigned long long)(last[i] - first[i]) / 1000,
                last[i] > first[i] ? (k - 1) * 1e6 / (double)(last[i] - first[i]) : 0.0, ref0[i], ref1[i]);
    return 0;
}

unsigned cap_light_frames;

bool cap_follow(struct cap_cam *c)
{
    unsigned m = c->mean;
    if (m >= CAP_TARGET * CAP_LOW && m <= CAP_TARGET * CAP_HIGH)
        return false;
    /* half the grab's step (the ratio, not its square; at most x2 / 1/2): a stream is corrected again a few frames
     * on, a full step each time would swing around the target */
    float r = m < CAP_BLACK ? 2.0f : (float)CAP_TARGET / (float)m;
    r = r > 2.0f ? 2.0f : r < 0.5f ? 0.5f : r;
    return set_exposure(c, c->exp * r);
}

void cap_close_all(struct cap_cam *cams, int n)
{
    cap_release_all(cams, n, NULL, NULL);
    stop_all(cams, n);
    /* a failed exit leaves VB initialized system-wide: the next program's init fails (leakcam_stream vb_teardown) */
    if (vb_ready && kd_mpi_vb_exit())
        fprintf(stderr, "cap: vb exit failed: the next program's VB init will fail\n");
    vb_ready = 0;
}
