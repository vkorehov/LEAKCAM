/*
 * RT-Smart camera backend: k230_sdk MPP VICAP, both OV5647 at 1280x960 (2x2 binned, full field
 * of view, 45 fps), one NV12 frame each.
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

#define IN_BUF_NUM  3              /* raw input buffers per device (offline mode), as the sample */
#define OUT_BUF_NUM 3              /* NV12 output buffers per channel */

/* slot -> sensor mode; both are the 1280x960@45 binned mode of the same register table.
 * k230_rtos_sdk / CanMV MPP (default) names them without the OV_ prefix and only with
 * CONFIG_MPP_ENABLE_SENSOR_OV5647; the older k230_sdk MPP has fixed OV_ values 45 and 48. */
#ifdef LEAKCAM_MPP_K230SDK
static const k_vicap_sensor_type slot_sensor[2] = {
    OV_OV5647_MIPI_CSI0_1280X960_45FPS_10BIT_LINEAR,   /* CAM2, J4, CSI0 */
    OV_OV5647_MIPI_CSI2_1280X960_45FPS_10BIT_LINEAR,   /* CAM1, J5, CSI2 */
};
#else
static const k_vicap_sensor_type slot_sensor[2] = {
    OV5647_MIPI_CSI0_1280X960_45FPS_10BIT_LINEAR,      /* CAM2, J4, CSI0 */
    OV5647_MIPI_CSI2_1280X960_45FPS_10BIT_LINEAR,      /* CAM1, J5, CSI2 */
};
#endif

static int vb_ready, dev_started[CAP_MAX_CAMS], dev_inited[CAP_MAX_CAMS];

int cap_open_all(struct cap_cam *cams, int n, unsigned width, unsigned height)
{
    k_vb_config vb;
    memset(&vb, 0, sizeof(vb));
    vb.max_pool_cnt = 2 * CAP_MAX_CAMS;
    int pool = 0;
    k_u32 out_size[CAP_MAX_CAMS] = { 0 };

    for (int i = 0; i < n; i++) {
        struct cap_cam *c = &cams[i];
        c->fd = -1;
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
        dev.buffer_size = VICAP_ALIGN_UP(c->width * c->height * 2, VICAP_ALIGN_1K);
        dev.pipe_ctrl.data = 0xFFFFFFFF;          /* the sample's default: every ISP block on */
        dev.pipe_ctrl.bits.af_enable = 0;
        dev.pipe_ctrl.bits.ahdr_enable = 0;
        dev.pipe_ctrl.bits.dnr3_enable = 0;       /* 3DNR needs extra reference buffers */
        dev.pipe_ctrl.bits.ae_enable = 1;
        dev.pipe_ctrl.bits.awb_enable = 1;
        dev.cpature_frame = 0;                    /* continuous; we stop the stream ourselves */
        dev.dw_enable = K_FALSE;
        if (kd_mpi_vicap_set_dev_attr((k_vicap_dev)c->node, dev)) {
            fprintf(stderr, "vicap: dev %u set_dev_attr failed\n", c->node);
            return -1;
        }

        vb.comm_pool[pool].blk_cnt = IN_BUF_NUM;
        vb.comm_pool[pool].blk_size = dev.buffer_size;
        vb.comm_pool[pool].mode = VB_REMAP_MODE_NOCACHE;
        pool++;
        out_size[i] = VICAP_ALIGN_UP(c->width * c->height * 3 / 2, VICAP_ALIGN_1K);
        vb.comm_pool[pool].blk_cnt = OUT_BUF_NUM;
        vb.comm_pool[pool].blk_size = out_size[i];
        vb.comm_pool[pool].mode = VB_REMAP_MODE_NOCACHE;
        pool++;
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
        chn.out_win.width = c->width;
        chn.out_win.height = c->height;
        chn.crop_win = chn.out_win;
        chn.scale_win = chn.out_win;
        chn.crop_enable = K_FALSE;
        chn.scale_enable = K_FALSE;
        chn.chn_enable = K_TRUE;
        chn.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        chn.buffer_num = OUT_BUF_NUM;
        chn.buffer_size = out_size[i];
        chn.fps = 0;                              /* sensor rate */
        kd_mpi_vicap_set_dump_reserved((k_vicap_dev)c->node, VICAP_CHN_ID_0, K_TRUE);
        if (kd_mpi_vicap_set_chn_attr((k_vicap_dev)c->node, VICAP_CHN_ID_0, chn)) {
            fprintf(stderr, "vicap: dev %u set_chn_attr failed\n", c->node);
            return -1;
        }
        c->luma = malloc((size_t)c->width * c->height);
        if (!c->luma)
            return -1;
    }
    for (int i = 0; i < n; i++) {
        if (kd_mpi_vicap_init((k_vicap_dev)cams[i].node)) {
            fprintf(stderr, "vicap: dev %u init failed (sensor not answering on its I2C bus?)\n",
                    cams[i].node);
            return -1;
        }
        dev_inited[i] = 1;
    }
    return 0;
}

int cap_grab_all(struct cap_cam *cams, int n, unsigned settle, int timeout_ms)
{
    for (int i = 0; i < n; i++) {
        cams[i].frames = 0;
        if (kd_mpi_vicap_start_stream((k_vicap_dev)cams[i].node)) {
            fprintf(stderr, "vicap: dev %u start_stream failed\n", cams[i].node);
            return -1;
        }
        dev_started[i] = 1;
    }
    /* Round-robin over the cameras: both stream at once, so dropping the AE settle frames on one
     * overlaps with the other. dump_frame blocks until that device's next frame. */
    int done = 0;
    while (done < n) {
        for (int i = 0; i < n; i++) {
            struct cap_cam *c = &cams[i];
            if (c->frames > settle)
                continue;
            k_video_frame_info f;
            memset(&f, 0, sizeof(f));
            if (kd_mpi_vicap_dump_frame((k_vicap_dev)c->node, VICAP_CHN_ID_0, VICAP_DUMP_YUV, &f,
                                        (k_u32)timeout_ms)) {
                fprintf(stderr, "vicap: dev %u no frame within %d ms, %d of %d cameras delivered\n",
                        c->node, timeout_ms, done, n);
                return -1;
            }
            if (++c->frames == settle + 1) {
                unsigned stride = f.v_frame.stride[0] ? f.v_frame.stride[0] : f.v_frame.width;
                k_u32 ysize = stride * f.v_frame.height;
                const k_u8 *y = kd_mpi_sys_mmap(f.v_frame.phys_addr[0], ysize);
                if (!y) {
                    kd_mpi_vicap_dump_release((k_vicap_dev)c->node, VICAP_CHN_ID_0, &f);
                    fprintf(stderr, "vicap: dev %u mmap of the frame failed\n", c->node);
                    return -1;
                }
                /* NV12: the first height rows (row stride) are luminance */
                for (unsigned r = 0; r < c->height; r++)
                    memcpy(c->luma + (size_t)r * c->width, y + (size_t)r * stride, c->width);
                kd_mpi_sys_munmap((void *)y, ysize);
                done++;
            }
            kd_mpi_vicap_dump_release((k_vicap_dev)c->node, VICAP_CHN_ID_0, &f);
        }
    }
    for (int i = 0; i < n; i++)
        if (dev_started[i]) {
            kd_mpi_vicap_stop_stream((k_vicap_dev)cams[i].node);
            dev_started[i] = 0;
        }
    return 0;
}

void cap_close_all(struct cap_cam *cams, int n)
{
    for (int i = 0; i < n; i++) {
        if (dev_started[i])
            kd_mpi_vicap_stop_stream((k_vicap_dev)cams[i].node);
        if (dev_inited[i])
            kd_mpi_vicap_deinit((k_vicap_dev)cams[i].node);
        dev_started[i] = dev_inited[i] = 0;
        free(cams[i].luma);
        cams[i].luma = NULL;
    }
    if (vb_ready)
        kd_mpi_vb_exit();
    vb_ready = 0;
}
