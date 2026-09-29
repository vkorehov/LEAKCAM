/*
 * leakcam_stream: PoC streaming from both LEAKCAM OV5647 cameras on RT-Smart (K230D).
 *
 * Video is always with audio: H.264 per camera plus the microphone as G.711 mu-law, 8 kHz mono.
 *   RTSP  (live555 via the SDK's librtsp_server.a):  rtsp://<ip>:8554/cam0, /cam1, both with the
 *         microphone (G711U)
 *
 * Pipeline per camera N (N = VICAP dev 0 = CSI0/J4, dev 1 = CSI2/J5):
 *   sensor -> VICAP dev N (offline mode, raw to DDR) -> ISP -> VICAP chn 0 (NV12 1280x960)
 *
 *   cap thread: kd_mpi_vicap_dump_frame -> kd_mpi_venc_send_frame(H.264 chn N)
 *               -> kd_mpi_vicap_dump_release
 *   Pattern: examples/mpp/sample_uvc_dev_vicap/main.c (dump -> send_frame -> release). Frames
 *   are not copied.
 *
 *   venc thread per chn: kd_mpi_venc_get_stream -> mmap packs -> RTSP or the upload -> release
 *   audio thread: kd_mpi_ai_get_frame (40 ms, inner codec left input = the mic) -> mu-law -> every
 *                 camera's RTSP session or upload. The AI is enabled first: its codec power-up
 *                 (~2 s) is over by the first video frame.
 *
 * VENC channels: 0,1 = H.264 cam0/cam1.
 *
 * Push mode, run by leakcam_wake when the server reports a leak:
 *   leakcam_stream -p <host>:<port> -t <seconds>
 * both cameras for <seconds>, each as one chunked POST /v1/video?cam=<N>: an FLV (flv.h) with the
 * camera's H.264, starting on an IDR, and the microphone. No RTSP. Exit 0 when both uploads were
 * answered 200.
 *
 * Not run on hardware yet (no boards, and the BL616 Wi-Fi driver is still to come).
 */
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>

#include "k_type.h"
#include "k_audio_comm.h"
#include "k_ai_comm.h"
#include "k_acodec_comm.h"
#include "k_module.h"
#include "k_vb_comm.h"
#include "k_vicap_comm.h"
#include "k_venc_comm.h"
#include "k_video_comm.h"
#include "mpi_ai_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
#include "mpi_vicap_api.h"

#include "flv.h"
#include "netclient.h"
#include "rtsp_glue.h"

#define NCAM            2
#define WIDTH           1280            /* OV5647 binned mode, see vicap_cap.c */
#define HEIGHT          960
#define FPS             30              /* VICAP chn fps (sensor runs 45); venc src=dst=FPS */
#define H264_KBPS       1500            /* per camera; k_venc_cbr.bit_rate is kbps (sample_venc.c) */
#define RAW_BUF_NUM     3               /* offline-mode raw buffers per device (all SDK samples: 3) */
#define YUV_BUF_NUM     4               /* NV12 output buffers per VICAP channel */
#define H264_STREAM_BLK 4               /* VENC output (bitstream) VB blocks per channel */
#define RTSP_PORT       8554
#define AUDIO_FRAME     (FLV_AUDIO_RATE / 25)   /* 320 samples = 40 ms, as the SDK's sample_audio */
#define AUDIO_QUEUE     50              /* AI frames buffered: 2 s */
#define MIC_GAIN_DB     30              /* inner codec mic PGA: 0, 6, 20 or 30 dB */
#define AI_DEV          0               /* the inner codec is behind I2S device 0 */
#define AI_CHN          0

#define ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))
#define NV12_SIZE       ALIGN_UP(WIDTH * HEIGHT * 3 / 2, 0x1000)
#define RAW_SIZE        ALIGN_UP(WIDTH * HEIGHT * 2, 0x400)
#define STREAM_SIZE     ALIGN_UP(WIDTH * HEIGHT / 2, 0x1000)   /* as kdmedia CreateVencVBPool */

#define H264_CHN(n)     (n)

static const k_vicap_sensor_type slot_sensor[NCAM] = {
    OV5647_MIPI_CSI0_1280X960_45FPS_10BIT_LINEAR,   /* CAM2, J4, CSI0 -> VICAP dev 0 */
    OV5647_MIPI_CSI2_1280X960_45FPS_10BIT_LINEAR,   /* CAM1, J5, CSI2 -> VICAP dev 1 */
};
static const char *session_name[NCAM] = { "cam0", "cam1" };

static k_u32 stream_pool[NCAM] = { VB_INVALID_POOLID, VB_INVALID_POOLID };
static volatile int g_run = 1;
static volatile int g_want_idr[NCAM];
static pthread_t cap_tid[NCAM], venc_tid[NCAM];
static int push_fd[NCAM] = { -1, -1 };    /* push mode: the upload per camera, else -1 */
static pthread_mutex_t push_mu[NCAM] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };
static struct flv_video push_flv[NCAM];   /* one FLV per upload: video and audio tags interleave */
static uint8_t push_au[NCAM][STREAM_SIZE];  /* the access unit being collected from VENC packs */
static size_t push_au_len[NCAM];
static volatile uint64_t g_t0_ms;         /* stream time 0: the cameras started; 0 = not yet */
static bool g_push;                       /* push mode (-p), else RTSP */
static int g_ncam = NCAM;
static pthread_t audio_tid;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
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
    pc.blk_cnt = H264_STREAM_BLK;
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
    a.venc_attr.type = K_PT_H264;
    a.venc_attr.pic_width = WIDTH;
    a.venc_attr.pic_height = HEIGHT;
    a.venc_attr.profile = VENC_PROFILE_H264_MAIN;
    a.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
    a.rc_attr.cbr.gop = 2 * FPS;        /* IDR every 2 s; new RTSP clients also request one */
    a.rc_attr.cbr.src_frame_rate = FPS;
    a.rc_attr.cbr.dst_frame_rate = FPS;
    a.rc_attr.cbr.bit_rate = H264_KBPS;
    if (kd_mpi_venc_create_chn(chn, &a)) {
        fprintf(stderr, "venc %u: create_chn failed\n", chn);
        return -1;
    }
    if (kd_mpi_venc_enable_idr(chn, K_TRUE))
        fprintf(stderr, "venc %u: enable_idr failed\n", chn);
    if (kd_mpi_venc_start_chn(chn)) {
        fprintf(stderr, "venc %u: start_chn failed\n", chn);
        return -1;
    }
    return 0;
}

static void venc_destroy(k_u32 chn)
{
    kd_mpi_venc_stop_chn(chn);
    kd_mpi_venc_detach_vb_pool(chn);
    kd_mpi_venc_destroy_chn(chn);
    if (stream_pool[chn] != VB_INVALID_POOLID)
        kd_mpi_vb_destory_pool(stream_pool[chn]);
    stream_pool[chn] = VB_INVALID_POOLID;
}

/* Pull one encoded frame (all its packs) from a VENC channel into sink. -1: none in time */
typedef void (*pack_sink)(int cam, const k_u8 *data, k_u32 len, k_u64 pts);

static int venc_pull(k_u32 chn, int cam, pack_sink sink, k_s32 timeout_ms)
{
    k_venc_chn_status st;
    k_venc_stream s;
    k_venc_pack packs[8];
    memset(&s, 0, sizeof(s));
    if (kd_mpi_venc_query_status(chn, &st))
        return -1;
    s.pack_cnt = st.cur_packs > 0 ? st.cur_packs : 1;
    if (s.pack_cnt > 8)
        s.pack_cnt = 8;
    s.pack = packs;
    if (kd_mpi_venc_get_stream(chn, &s, timeout_ms))
        return -1;
    for (k_u32 i = 0; i < s.pack_cnt; i++) {
        k_u8 *p = kd_mpi_sys_mmap(s.pack[i].phys_addr, s.pack[i].len);
        if (!p)
            continue;
        sink(cam, p, s.pack[i].len, s.pack[i].pts);
        kd_mpi_sys_munmap(p, s.pack[i].len);
    }
    kd_mpi_venc_release_stream(chn, &s);
    return 0;
}

static void rtsp_sink(int cam, const k_u8 *data, k_u32 len, k_u64 pts)
{
    rtsp_glue_send(session_name[cam], data, len, pts);   /* copies; SPS/PPS/IDR parsed inside */
}

/* collect the frame's packs: an FLV video tag needs the whole access unit */
static void push_sink(int cam, const k_u8 *data, k_u32 len, k_u64 pts)
{
    (void)pts;
    if (push_au_len[cam] + len <= sizeof(push_au[cam])) {
        memcpy(push_au[cam] + push_au_len[cam], data, len);
        push_au_len[cam] += len;
    }
}

/* flv_sink into camera N's chunked upload; push_mu[N] held by the caller */
static int push_write(void *ctx, const void *p, size_t n)
{
    int cam = (int)(long)ctx;
    if (push_fd[cam] < 0)
        return -1;
    if (http_chunk(push_fd[cam], p, n) < 0) {
        fprintf(stderr, "push cam%d: upload broke off\n", cam);
        close(push_fd[cam]);
        push_fd[cam] = -1;
        return -1;
    }
    return 0;
}

static void *venc_thread(void *arg)
{
    int cam = (int)(long)arg;
    while (g_run) {
        if (g_want_idr[cam]) {                            /* new RTSP client: start on an IDR */
            g_want_idr[cam] = 0;
            kd_mpi_venc_request_idr(H264_CHN(cam));
        }
        bool push = push_fd[cam] >= 0;
        if (venc_pull(H264_CHN(cam), cam, push ? push_sink : rtsp_sink, 1000) == 0 && push) {
            pthread_mutex_lock(&push_mu[cam]);
            flv_video(&push_flv[cam], push_write, (void *)(long)cam, push_au[cam], push_au_len[cam],
                      (uint32_t)(now_ms() - g_t0_ms));
            pthread_mutex_unlock(&push_mu[cam]);
        }
        push_au_len[cam] = 0;
    }
    return NULL;
}

/* RTSP PLAY hook, runs on the live555 thread: only flag it */
static void on_play(const char *session, size_t clients, void *user)
{
    (void)user;
    for (int i = 0; i < NCAM; i++)
        if (!strcmp(session, session_name[i]))
            g_want_idr[i] = 1;
    printf("rtsp: %s play, %u client(s)\n", session, (unsigned)clients);
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
    a.kd_audio_attr.i2s_attr.sample_rate = FLV_AUDIO_RATE;
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
    k_u32 gain = MIC_GAIN_DB;
    if (fd < 0 || ioctl(fd, k_acodec_set_gain_micl, &gain))
        fprintf(stderr, "mic: gain not set\n");
    if (fd >= 0)
        close(fd);
    return 0;
}

static void mic_close(void)
{
    kd_mpi_ai_disable_chn(AI_DEV, AI_CHN);
    kd_mpi_ai_disable(AI_DEV);
}

/* 40 ms frames to every camera's stream; stamped by the samples sent, from the moment the cameras
 * started, so audio and video share one clock */
static void *audio_thread(void *arg)
{
    (void)arg;
    uint64_t sent = 0, start_ms = 0;
    uint8_t u[AUDIO_FRAME];
    while (g_run) {
        k_audio_frame fr;
        if (kd_mpi_ai_get_frame(AI_DEV, AI_CHN, &fr, 100))
            continue;
        int16_t *pcm = kd_mpi_sys_mmap(fr.phys_addr, fr.len);
        size_t n = pcm ? fr.len / 2 : 0;
        if (n > AUDIO_FRAME)
            n = AUDIO_FRAME;
        for (size_t i = 0; i < n; i++)
            u[i] = flv_ulaw(pcm[i]);
        if (pcm)
            kd_mpi_sys_munmap(pcm, fr.len);
        kd_mpi_ai_release_frame(AI_DEV, AI_CHN, &fr);
        if (!g_t0_ms || !n)
            continue;                                 /* before the cameras: codec settling */
        if (!sent)
            start_ms = now_ms() - g_t0_ms;
        uint32_t ms = (uint32_t)(start_ms + sent * 1000 / FLV_AUDIO_RATE);
        sent += n;
        for (int cam = 0; cam < g_ncam; cam++) {
            if (g_push) {
                pthread_mutex_lock(&push_mu[cam]);
                flv_audio(push_write, (void *)(long)cam, u, n, ms);   /* no-op once it broke off */
                pthread_mutex_unlock(&push_mu[cam]);
            } else {
                rtsp_glue_send_audio(session_name[cam], u, n, ms);
            }
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
    kd_mpi_vicap_set_dump_reserved(dev, VICAP_CHN_ID_0, K_TRUE);
    if (kd_mpi_vicap_set_chn_attr(dev, VICAP_CHN_ID_0, c)) {
        fprintf(stderr, "vicap %d: set_chn_attr chn0 failed\n", cam);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ capture thread */

static void *cap_thread(void *arg)
{
    int cam = (int)(long)arg;
    const k_vicap_chn jchn = VICAP_CHN_ID_0;
    while (g_run) {
        k_video_frame_info f;
        memset(&f, 0, sizeof(f));
        if (kd_mpi_vicap_dump_frame((k_vicap_dev)cam, jchn, VICAP_DUMP_YUV, &f, 1000)) {
            fprintf(stderr, "vicap %d: no frame in 1 s\n", cam);
            continue;
        }
        kd_mpi_venc_send_frame(H264_CHN(cam), &f, 1000);
        /* sample_uvc_dev_vicap releases right after send_frame: VENC holds its own
         * reference on the VB block (not verifiable in source: libvenc/libvpu are binary) */
        kd_mpi_vicap_dump_release((k_vicap_dev)cam, jchn, &f);
    }
    return NULL;
}

/* ------------------------------------------------------------------ main */

static void on_sig(int sig) { (void)sig; g_run = 0; }

int main(int argc, char **argv)
{
    int ncam = NCAM, seconds = 0, status = 0;
    bool mic_on = false;
    char host[64] = "", port[8] = "";
    int opt;
    while ((opt = getopt(argc, argv, "p:t:")) != -1) {
        if (opt == 'p' && sscanf(optarg, "%63[^:]:%7s", host, port) == 2)
            continue;
        if (opt == 't' && (seconds = atoi(optarg)) > 0)
            continue;
        fprintf(stderr, "usage: %s [ncam] | -p host:port -t seconds\n", argv[0]);
        return 2;
    }
    bool push = g_push = host[0] != 0;
    if (!push && optind < argc)
        ncam = atoi(argv[optind]);                    /* 1 = camera 0 only */
    if (ncam < 1 || ncam > NCAM)
        ncam = NCAM;
    g_ncam = ncam;
    if (push && seconds <= 0)
        seconds = 5;
    signal(SIGINT, on_sig);
    signal(SIGPIPE, SIG_IGN);

    /* 1. VB, the microphone (its codec powers up while the rest is set up), 2. VENC (pools
     * attached before create; start before frames arrive) */
    if (vb_setup())
        return 1;
    if (mic_open())
        goto out;
    mic_on = true;
    pthread_create(&audio_tid, NULL, audio_thread, NULL);
    for (int i = 0; i < ncam; i++)
        if (venc_create(H264_CHN(i)))
            goto out;

    /* 3. push: one chunked upload per camera, opened before the first frame. Otherwise RTSP
     * sessions (network must be up for clients, not for Init) */
    if (push) {
        for (int i = 0; i < ncam; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/v1/video?cam=%d", i);
            push_fd[i] = net_connect(host, port, 10);
            if (push_fd[i] < 0 || http_post(push_fd[i], host, path, "video/x-flv", -1) < 0 ||
                flv_header(push_write, (void *)(long)i) < 0) {
                fprintf(stderr, "push cam%d: %s:%s not reached\n", i, host, port);
                goto out;
            }
            g_want_idr[i] = 1;                        /* the upload starts on an IDR */
        }
    } else {
        if (rtsp_glue_init(RTSP_PORT, on_play, NULL) < 0)
            goto out;
        for (int i = 0; i < ncam; i++)
            if (rtsp_glue_add_session(session_name[i]) < 0)            /* H.264 + G711U */
                goto out;
        rtsp_glue_start();
    }

    /* 4. VICAP: set_dev_attr/set_chn_attr on every device, then init every device */
    for (int i = 0; i < ncam; i++)
        if (vicap_setup(i))
            goto out;
    for (int i = 0; i < ncam; i++)
        if (kd_mpi_vicap_init((k_vicap_dev)i)) {
            fprintf(stderr, "vicap %d: init failed (sensor on its I2C bus?)\n", i);
            goto out;
        }

    /* 5. threads, then start streaming */
    for (int i = 0; i < ncam; i++) {
        pthread_create(&venc_tid[i], NULL, venc_thread, (void *)(long)i);
        pthread_create(&cap_tid[i], NULL, cap_thread, (void *)(long)i);
    }
    for (int i = 0; i < ncam; i++)
        if (kd_mpi_vicap_start_stream((k_vicap_dev)i)) {
            fprintf(stderr, "vicap %d: start_stream failed\n", i);
            g_run = 0;
        }
    g_t0_ms = now_ms();                                /* audio and video stamps count from here */
    for (int i = 0; !push && i < ncam; i++)
        rtsp_glue_print_url(session_name[i]);

    for (int left = seconds; g_run && (!push || left > 0); left--)
        sleep(1);
    g_run = 0;

    for (int i = 0; i < ncam; i++) {
        pthread_join(cap_tid[i], NULL);
        pthread_join(venc_tid[i], NULL);
    }
    pthread_join(audio_tid, NULL);
    mic_on = false;
    mic_close();
    for (int i = 0; push && i < ncam; i++) {        /* end the bodies, then the server answers */
        char reply[128];
        int st = push_fd[i] >= 0 && http_chunk(push_fd[i], NULL, 0) == 0 ? http_reply(push_fd[i], reply, sizeof(reply)) : -1;
        if (st != 200) {
            fprintf(stderr, "push cam%d: server status %d\n", i, st);
            status = 1;
        }
    }
    goto done;
out:
    status = 1;
done:
    g_run = 0;
    if (mic_on) {                                      /* failed on the way up */
        pthread_join(audio_tid, NULL);
        mic_close();
    }
    for (int i = 0; i < ncam; i++) {
        kd_mpi_vicap_stop_stream((k_vicap_dev)i);
        kd_mpi_vicap_deinit((k_vicap_dev)i);
    }
    if (!push)
        rtsp_glue_stop();
    for (int i = 0; i < ncam; i++) {
        venc_destroy(H264_CHN(i));
        if (push_fd[i] >= 0)
            close(push_fd[i]);
    }
    kd_mpi_venc_close_fd();
    kd_mpi_vb_exit();
    return status;
}
