/*
 * leakcam_stream: both LEAKCAM OV5647 cameras live to the server on RT-Smart (K230D), video always
 * with audio: H.265 (Main) per camera plus the microphone as Opus, over Enhanced RTMP (rtmp.h).
 *
 *   leakcam_stream -p <host>:<port> -t <seconds>     (run by leakcam_wake for the clip the server asks for)
 *
 * Each camera is published as rtmp://<host>:<port>/leakcam/cam<N>, starting on an IDR; both carry
 * the same microphone track. Exit 0 when both streams stayed up to the end. For the bench, point
 * it at a PC running MediaMTX (or `ffplay -listen 1 rtmp://0.0.0.0:1935/leakcam/cam0`, FFmpeg 7.1+)
 * with a long -t.
 *
 * Pipeline per camera N (N = VICAP dev 0 = CSI0/J4, dev 1 = CSI2/J5), hardware end to end:
 *   sensor -> VICAP dev N (offline mode, raw to DDR) -> ISP -> VICAP chn 0 (NV12 1280x960)
 *          -> kd_mpi_sys_bind -> VENC chn N (H.265), no CPU per frame
 *          (order and teardown as examples/mpp/sample_webrtc/mpp_pipeline.c)
 *   venc thread: kd_mpi_venc_get_stream -> the access unit -> rtmp_video -> release
 * Audio: kd_mpi_ai_get_frame (40 ms at 16 kHz, the inner codec's left input = the mic) -> Opus ->
 * rtmp_audio on every camera's stream. The AI is enabled first: its codec power-up (~2 s) is over
 * by the first video frame.
 *
 * Not run on hardware yet (no boards, and the BL616 Wi-Fi driver is still to come).
 */
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "k_type.h"
#include "k_audio_comm.h"
#include "k_ai_comm.h"
#include "k_acodec_comm.h"
#include "k_module.h"
#include "k_vb_comm.h"
#include "k_vicap_comm.h"
#include "k_venc_comm.h"
#include "mpi_ai_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
#include "mpi_vicap_api.h"

#include "opus.h"
#include "rtmp.h"

#define NCAM            2
#define WIDTH           1280            /* OV5647 binned mode, see vicap_cap.c */
#define HEIGHT          960
/* Battery: every byte is Wi-Fi airtime and every frame ISP + encoder work. A fixed camera on a
 * mostly still room needs little of either; raise them only if the board shows a clip too poor */
#define FPS             15              /* VICAP chn fps (sensor runs 45); venc src=dst=FPS */
#define VIDEO_KBPS      500             /* per camera; k_venc_cbr.bit_rate is kbps (sample_venc.c) */
#define GOP_S           10              /* a 5 s clip starts on a requested IDR: one is enough */
#define RAW_BUF_NUM     3               /* offline-mode raw buffers per device (all SDK samples: 3) */
#define YUV_BUF_NUM     4               /* NV12 output buffers per VICAP channel */
#define STREAM_BLK      4               /* VENC output (bitstream) VB blocks per channel */

/* Audio: Opus is the most bit-efficient codec phones and browsers play; wideband keeps the hiss
 * of a spray and the ticks of a drip. No DTX: it would call a quiet leak silence and drop it */
#define AUDIO_RATE      16000
#define AUDIO_FRAME     (AUDIO_RATE / 25)   /* 640 samples = 40 ms: one AI frame = one Opus packet */
#define AUDIO_QUEUE     50              /* AI frames buffered: 2 s */
#define OPUS_KBPS       24
#define OPUS_EFFORT     3               /* complexity 0-10: 3 is a few % of a core at 16 kHz mono */
/* Leaks are quiet: all the gain goes in front of the ADC, where it lifts the sound over the
 * converter's noise instead of amplifying both. PGA 30 dB + ALC 24 dB clips near 80 dB SPL,
 * which a door slam may reach and a leak never does */
#define MIC_PGA_DB      30              /* inner codec mic PGA: 0, 6, 20 or 30 dB */
#define MIC_ALC_DB      24.0f           /* ALC analog gain, -18..28.5 dB in 1.5 dB steps */
#define AI_DEV          0               /* the inner codec is behind I2S device 0 */
#define AI_CHN          0

#define ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))
#define NV12_SIZE       ALIGN_UP(WIDTH * HEIGHT * 3 / 2, 0x1000)
#define RAW_SIZE        ALIGN_UP(WIDTH * HEIGHT * 2, 0x400)
#define STREAM_SIZE     ALIGN_UP(WIDTH * HEIGHT / 2, 0x1000)   /* as kdmedia CreateVencVBPool */

static const k_vicap_sensor_type slot_sensor[NCAM] = {
    OV5647_MIPI_CSI0_1280X960_45FPS_10BIT_LINEAR,   /* CAM2, J4, CSI0 -> VICAP dev 0 */
    OV5647_MIPI_CSI2_1280X960_45FPS_10BIT_LINEAR,   /* CAM1, J5, CSI2 -> VICAP dev 1 */
};
static const char *stream_name[NCAM] = { "cam0", "cam1" };

static k_u32 stream_pool[NCAM] = { VB_INVALID_POOLID, VB_INVALID_POOLID };
static volatile int g_run = 1;
static volatile int g_want_idr[NCAM];
static pthread_t venc_tid[NCAM], audio_tid;
static struct rtmp out[NCAM] = { { .fd = -1 }, { .fd = -1 } };
static pthread_mutex_t out_mu[NCAM] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };
static uint8_t au[NCAM][STREAM_SIZE];     /* the access unit being collected from VENC packs */
static size_t au_len[NCAM];
static volatile uint64_t g_t0_ms;         /* stream time 0: the cameras started; 0 = not yet */
static OpusEncoder *g_opus;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* out_mu[cam] held: a send that fails ends that camera's stream, the other goes on */
static void send_result(int cam, int rc)
{
    if (rc && out[cam].fd >= 0) {
        fprintf(stderr, "cam%d: stream broke off\n", cam);
        rtmp_close(&out[cam]);
    }
}

/* ------------------------------------------------------------------ VB + VENC */

static int vb_setup(void)
{
    k_vb_config vb;
    memset(&vb, 0, sizeof(vb));
    vb.max_pool_cnt = 64;               /* no common pools: VICAP auto-creates its own
                                           (buffer_pool_id = VB_INVALID_POOLID) */
    if (kd_mpi_vb_set_config(&vb) || kd_mpi_vb_init()) {
        fprintf(stderr, "vb init failed\n");
        return -1;
    }
    return 0;
}

static int venc_create(k_u32 chn)
{
    k_vb_pool_config pc;
    memset(&pc, 0, sizeof(pc));
    pc.blk_cnt = STREAM_BLK;
    pc.blk_size = STREAM_SIZE;
    pc.mode = VB_REMAP_MODE_NOCACHE;
    stream_pool[chn] = kd_mpi_vb_create_pool(&pc);
    if (stream_pool[chn] == VB_INVALID_POOLID) {
        fprintf(stderr, "venc %u: stream pool failed (MMZ full?)\n", chn);
        return -1;
    }
    kd_mpi_venc_attach_vb_pool(chn, stream_pool[chn]);   /* before create_chn, as the samples */

    k_venc_chn_attr a;
    memset(&a, 0, sizeof(a));
    a.venc_attr.type = K_PT_H265;
    a.venc_attr.pic_width = WIDTH;
    a.venc_attr.pic_height = HEIGHT;
    a.venc_attr.profile = VENC_PROFILE_H265_MAIN;
    a.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
    a.rc_attr.cbr.gop = GOP_S * FPS;    /* each stream also requests an IDR at its start */
    a.rc_attr.cbr.src_frame_rate = FPS;
    a.rc_attr.cbr.dst_frame_rate = FPS;
    a.rc_attr.cbr.bit_rate = VIDEO_KBPS;
    if (kd_mpi_venc_create_chn(chn, &a)) {
        fprintf(stderr, "venc %u: create_chn failed\n", chn);
        return -1;
    }
    if (kd_mpi_venc_enable_idr(chn, K_TRUE))
        fprintf(stderr, "venc %u: enable_idr failed\n", chn);
    return 0;
}

/* VICAP dev N chn 0 -> VENC chn N: frames go from the ISP to the encoder without the CPU */
static int bind(int cam, bool on)
{
    k_mpp_chn vi = { .mod_id = K_ID_VI, .dev_id = cam, .chn_id = VICAP_CHN_ID_0 };
    k_mpp_chn venc = { .mod_id = K_ID_VENC, .dev_id = 0, .chn_id = cam };
    if (!on)
        return kd_mpi_sys_unbind(&vi, &venc);
    if (kd_mpi_sys_bind(&vi, &venc)) {
        fprintf(stderr, "cam%d: VICAP -> VENC bind failed\n", cam);
        return -1;
    }
    return 0;
}

static void venc_destroy(k_u32 chn)
{
    kd_mpi_venc_detach_vb_pool(chn);
    kd_mpi_venc_destroy_chn(chn);
    if (stream_pool[chn] != VB_INVALID_POOLID)
        kd_mpi_vb_destory_pool(stream_pool[chn]);
    stream_pool[chn] = VB_INVALID_POOLID;
}

/* one encoded frame (all its packs) of camera N into au[N]; -1: none in time */
static int venc_pull(int cam, k_s32 timeout_ms)
{
    k_venc_chn_status st;
    k_venc_stream s;
    k_venc_pack packs[8];
    memset(&s, 0, sizeof(s));
    if (kd_mpi_venc_query_status(cam, &st))
        return -1;
    s.pack_cnt = st.cur_packs > 0 ? st.cur_packs : 1;
    if (s.pack_cnt > 8)
        s.pack_cnt = 8;
    s.pack = packs;
    if (kd_mpi_venc_get_stream(cam, &s, timeout_ms))
        return -1;
    au_len[cam] = 0;
    for (k_u32 i = 0; i < s.pack_cnt; i++) {
        k_u8 *p = kd_mpi_sys_mmap(s.pack[i].phys_addr, s.pack[i].len);
        if (!p)
            continue;
        if (au_len[cam] + s.pack[i].len <= sizeof(au[cam])) {
            memcpy(au[cam] + au_len[cam], p, s.pack[i].len);
            au_len[cam] += s.pack[i].len;
        }
        kd_mpi_sys_munmap(p, s.pack[i].len);
    }
    kd_mpi_venc_release_stream(cam, &s);
    return 0;
}

static void *venc_thread(void *arg)
{
    int cam = (int)(long)arg;
    while (g_run) {
        if (g_want_idr[cam]) {                            /* the stream starts on an IDR */
            g_want_idr[cam] = 0;
            kd_mpi_venc_request_idr(cam);
        }
        if (venc_pull(cam, 1000))
            continue;
        pthread_mutex_lock(&out_mu[cam]);
        if (out[cam].fd >= 0)
            send_result(cam, rtmp_video(&out[cam], au[cam], au_len[cam], (uint32_t)(now_ms() - g_t0_ms)));
        pthread_mutex_unlock(&out_mu[cam]);
    }
    return NULL;
}

/* ------------------------------------------------------------------ microphone */

/* the inner codec's left input is the mic (the SDK's names are the EVB's: "left" is its headset
 * jack). Order as src/rtsmart/examples/mpp/sample_audio: pub attr -> enable (the first enable
 * powers the codec up, ~2 s, and resets its gains) -> enable chn -> gains */
static int mic_open(void)
{
    k_aio_dev_attr a;
    memset(&a, 0, sizeof(a));
    a.audio_type = KD_AUDIO_INPUT_TYPE_I2S;
    a.avsync = K_FALSE;
    a.kd_audio_attr.i2s_attr.sample_rate = AUDIO_RATE;
    a.kd_audio_attr.i2s_attr.bit_width = KD_AUDIO_BIT_WIDTH_16;
    a.kd_audio_attr.i2s_attr.chn_cnt = 2;                     /* the SDK sample: always 2 on I2S */
    a.kd_audio_attr.i2s_attr.snd_mode = KD_AUDIO_SOUND_MODE_MONO;
    a.kd_audio_attr.i2s_attr.mono_channel = KD_I2S_IN_MONO_LEFT_CHANNEL;
    a.kd_audio_attr.i2s_attr.i2s_mode = K_STANDARD_MODE;
    a.kd_audio_attr.i2s_attr.frame_num = AUDIO_QUEUE;
    a.kd_audio_attr.i2s_attr.point_num_per_frame = AUDIO_FRAME;
    a.kd_audio_attr.i2s_attr.i2s_type = K_AIO_I2STYPE_INNERCODEC;
    if (kd_mpi_ai_set_pub_attr(AI_DEV, &a) || kd_mpi_ai_enable(AI_DEV) ||
        kd_mpi_ai_enable_chn(AI_DEV, AI_CHN)) {
        fprintf(stderr, "mic: AI enable failed\n");
        return -1;
    }
    int fd = open("/dev/acodec_device", O_RDWR);
    k_u32 pga = MIC_PGA_DB;
    float alc = MIC_ALC_DB;
    if (fd < 0 || ioctl(fd, k_acodec_set_gain_micl, &pga) || ioctl(fd, k_acodec_set_alc_gain_micl, &alc))
        fprintf(stderr, "mic: gain not set\n");
    if (fd >= 0)
        close(fd);

    int err;
    g_opus = opus_encoder_create(AUDIO_RATE, 1, OPUS_APPLICATION_AUDIO, &err);
    if (!g_opus) {
        fprintf(stderr, "mic: opus encoder: %s\n", opus_strerror(err));
        return -1;
    }
    opus_encoder_ctl(g_opus, OPUS_SET_BITRATE(OPUS_KBPS * 1000));
    opus_encoder_ctl(g_opus, OPUS_SET_COMPLEXITY(OPUS_EFFORT));
    opus_encoder_ctl(g_opus, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));   /* hiss and ticks, not speech */
    return 0;
}

static void mic_close(void)
{
    kd_mpi_ai_disable_chn(AI_DEV, AI_CHN);
    kd_mpi_ai_disable(AI_DEV);
    opus_encoder_destroy(g_opus);
    g_opus = NULL;
}

/* 40 ms frames to every camera's stream, stamped by the samples sent from the moment the cameras
 * started, so audio and video share one clock */
static void *audio_thread(void *arg)
{
    (void)arg;
    uint64_t sent = 0, start_ms = 0;
    static opus_int16 pcm[AUDIO_FRAME];
    uint8_t pkt[1000];
    while (g_run) {
        k_audio_frame fr;
        if (kd_mpi_ai_get_frame(AI_DEV, AI_CHN, &fr, 100))
            continue;
        const int16_t *p = kd_mpi_sys_mmap(fr.phys_addr, fr.len);
        bool whole = p && fr.len == sizeof(pcm);
        if (whole)
            memcpy(pcm, p, sizeof(pcm));
        if (p)
            kd_mpi_sys_munmap((void *)p, fr.len);
        kd_mpi_ai_release_frame(AI_DEV, AI_CHN, &fr);
        if (!g_t0_ms || !whole)
            continue;                                 /* before the cameras: codec settling */
        int n = opus_encode(g_opus, pcm, AUDIO_FRAME, pkt, sizeof(pkt));
        if (n <= 0)
            continue;
        if (!sent)
            start_ms = now_ms() - g_t0_ms;
        uint32_t ms = (uint32_t)(start_ms + sent * 1000 / AUDIO_RATE);
        sent += AUDIO_FRAME;
        for (int cam = 0; cam < NCAM; cam++) {
            pthread_mutex_lock(&out_mu[cam]);
            if (out[cam].fd >= 0)
                send_result(cam, rtmp_audio(&out[cam], pkt, (size_t)n, ms));
            pthread_mutex_unlock(&out_mu[cam]);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ VICAP */

static int vicap_setup(int cam)
{
    k_vicap_dev dev = (k_vicap_dev)cam;
    k_vicap_dev_attr d;
    memset(&d, 0, sizeof(d));
    if (kd_mpi_vicap_get_sensor_info(slot_sensor[cam], &d.sensor_info)) {
        fprintf(stderr, "vicap %d: sensor type not in this build\n", cam);
        return -1;
    }
    d.input_type = VICAP_INPUT_TYPE_SENSOR;
    d.acq_win.width = d.sensor_info.width;
    d.acq_win.height = d.sensor_info.height;
    d.mode = VICAP_WORK_OFFLINE_MODE;                 /* two sensors -> offline mode */
    d.buffer_num = RAW_BUF_NUM;
    d.buffer_size = RAW_SIZE;
    d.buffer_pool_id = VB_INVALID_POOLID;             /* auto-create (k_vicap_comm.h comment);
                                                         memset leaves 0 = a real pool id */
    d.pipe_ctrl.data = 0xFFFFFFFF;
    d.pipe_ctrl.bits.af_enable = 0;
    d.pipe_ctrl.bits.ahdr_enable = 0;
    d.pipe_ctrl.bits.dnr3_enable = 0;                 /* 3DNR costs reference buffers */
    d.cpature_frame = 0;
    d.dw_enable = K_FALSE;
    if (kd_mpi_vicap_set_dev_attr(dev, d)) {
        fprintf(stderr, "vicap %d: set_dev_attr failed\n", cam);
        return -1;
    }

    k_vicap_chn_attr c;
    memset(&c, 0, sizeof(c));
    c.out_win.width = WIDTH;                          /* 1280 = 16-aligned, as VENC wants */
    c.out_win.height = HEIGHT;
    c.crop_win = c.out_win;
    c.scale_win = c.out_win;
    c.crop_enable = K_FALSE;
    c.scale_enable = K_FALSE;
    c.chn_enable = K_TRUE;
    c.pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    c.buffer_num = YUV_BUF_NUM;
    c.buffer_size = NV12_SIZE;
    c.buffer_pool_id = VB_INVALID_POOLID;
    c.alignment = 12;                                 /* 4 KiB, every VENC sample uses 12 */
    c.fps = FPS;
    if (kd_mpi_vicap_set_chn_attr(dev, VICAP_CHN_ID_0, c)) {
        fprintf(stderr, "vicap %d: set_chn_attr chn0 failed\n", cam);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ main */

static void on_sig(int sig) { (void)sig; g_run = 0; }

int main(int argc, char **argv)
{
    int seconds = 0, status = 0, opt;
    bool mic_on = false, bound[NCAM] = { false }, streaming = false;
    char host[64] = "", port[8] = "";
    while ((opt = getopt(argc, argv, "p:t:")) != -1) {
        if (opt == 'p' && sscanf(optarg, "%63[^:]:%7s", host, port) == 2)
            continue;
        if (opt == 't' && (seconds = atoi(optarg)) > 0)
            continue;
        host[0] = 0;
        break;
    }
    if (!host[0] || seconds <= 0) {
        fprintf(stderr, "usage: %s -p host:port -t seconds\n", argv[0]);
        return 2;
    }
    signal(SIGINT, on_sig);
    signal(SIGPIPE, SIG_IGN);

    /* 1. VB, 2. the microphone (its codec powers up while the rest is set up), 3. VENC channels
     * (pools attached before create) */
    if (vb_setup())
        return 1;
    if (mic_open())
        goto done_err;
    mic_on = true;
    pthread_create(&audio_tid, NULL, audio_thread, NULL);
    for (int i = 0; i < NCAM; i++)
        if (venc_create(i))
            goto done_err;

    /* 4. one publish per camera, set up before the first frame, audio header first */
    for (int i = 0; i < NCAM; i++) {
        opus_int32 lookahead = 0;
        opus_encoder_ctl(g_opus, OPUS_GET_LOOKAHEAD(&lookahead));
        if (rtmp_publish(&out[i], host, port, "leakcam", stream_name[i], 10) ||
            rtmp_opus_header(&out[i], (unsigned)lookahead * 48000 / AUDIO_RATE, AUDIO_RATE))
            goto done_err;
        g_want_idr[i] = 1;
    }

    /* 5. VICAP: set_dev_attr/set_chn_attr on every device, then init every device */
    for (int i = 0; i < NCAM; i++)
        if (vicap_setup(i))
            goto done_err;
    for (int i = 0; i < NCAM; i++)
        if (kd_mpi_vicap_init((k_vicap_dev)i)) {
            fprintf(stderr, "vicap %d: init failed (sensor on its I2C bus?)\n", i);
            goto done_err;
        }

    /* 6. bind, then start the cameras and the encoders; both streams' clock starts with them */
    for (int i = 0; i < NCAM; i++) {
        if (bind(i, true))
            goto done_err;
        bound[i] = true;
    }
    for (int i = 0; i < NCAM; i++)
        pthread_create(&venc_tid[i], NULL, venc_thread, (void *)(long)i);
    streaming = true;
    for (int i = 0; i < NCAM; i++)
        if (kd_mpi_vicap_start_stream((k_vicap_dev)i) || kd_mpi_venc_start_chn(i)) {
            fprintf(stderr, "cam%d: start failed\n", i);
            g_run = 0;
        }
    g_t0_ms = now_ms();

    for (int left = seconds; g_run && left > 0; left--)
        sleep(1);
    goto done;
done_err:
    status = 1;
done:
    g_run = 0;
    if (streaming)
        for (int i = 0; i < NCAM; i++)
            pthread_join(venc_tid[i], NULL);
    if (mic_on) {
        pthread_join(audio_tid, NULL);
        mic_close();
    }
    for (int i = 0; i < NCAM; i++) {
        kd_mpi_venc_stop_chn(i);
        kd_mpi_vicap_stop_stream((k_vicap_dev)i);
        if (bound[i])
            bind(i, false);
        kd_mpi_vicap_deinit((k_vicap_dev)i);
        venc_destroy(i);
        if (out[i].fd < 0)
            status = 1;                               /* a stream that broke off fails the run */
        rtmp_close(&out[i]);
    }
    kd_mpi_venc_close_fd();
    kd_mpi_vb_exit();
    return status;
}
