/*
 * leakcam_stream: PoC streaming from both LEAKCAM OV5647 cameras on RT-Smart (K230D).
 *
 *   RTSP  (H.264, live555 via the SDK's librtsp_server.a):  rtsp://<ip>:8554/cam0, /cam1
 *   HTTP  (own tiny server on POSIX sockets / SAL / lwIP):
 *         GET /snap/0.jpg  /snap/1.jpg     one JPEG (VENC JPEG channel)
 *         GET /mjpeg/0     /mjpeg/1        multipart/x-mixed-replace MJPEG
 *
 * Pipeline per camera N (N = VICAP dev 0 = CSI0/J4, dev 1 = CSI2/J5):
 *   sensor -> VICAP dev N (offline mode, raw to DDR) -> ISP -> VICAP chn 0 (NV12 1280x960)
 *
 *   cap thread: kd_mpi_vicap_dump_frame -> kd_mpi_venc_send_frame(H.264 chn N)
 *               [+ kd_mpi_venc_send_frame(JPEG chn 2+N) when a JPEG is wanted]
 *               -> kd_mpi_vicap_dump_release
 *   Pattern: examples/mpp/sample_uvc_dev_vicap/main.c (dump -> send_frame -> release). The same
 *   NV12 frame feeds both encoders, so one VICAP channel per camera is enough (a VI -> VENC bind
 *   would need a second channel for the JPEGs: +5 MiB of MMZ per camera). Frames are not copied.
 *
 *   venc thread per H.264 chn: kd_mpi_venc_get_stream -> mmap packs -> rtsp_glue_send -> release
 *
 * VENC channels (VENC_MAX_CHN_NUMS = 4 in k_venc_comm.h): 0,1 = H.264 cam0/cam1,
 * 2,3 = JPEG cam0/cam1. All four are used.
 *
 * Not run on hardware yet (no boards, and the BL616 Wi-Fi driver is still to come).
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "k_type.h"
#include "k_module.h"
#include "k_vb_comm.h"
#include "k_vicap_comm.h"
#include "k_venc_comm.h"
#include "k_video_comm.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
#include "mpi_vicap_api.h"

#include "rtsp_glue.h"

#define NCAM            2
#define WIDTH           1280            /* OV5647 binned mode, see vicap_cap.c */
#define HEIGHT          960
#define FPS             30              /* VICAP chn fps (sensor runs 45); venc src=dst=FPS */
#define H264_KBPS       1500            /* per camera; k_venc_cbr.bit_rate is kbps (sample_venc.c) */
#define JPEG_QFACTOR    60              /* k_venc_mjpeg_fixqp.q_factor [1,99] */
#define MJPEG_EVERY     6               /* manual feed: JPEG every 6th frame (5 fps) while wanted */
#define RAW_BUF_NUM     3               /* offline-mode raw buffers per device (all SDK samples: 3) */
#define YUV_BUF_NUM     4               /* NV12 output buffers per VICAP channel */
#define H264_STREAM_BLK 4               /* VENC output (bitstream) VB blocks per channel */
#define JPEG_STREAM_BLK 2
#define RTSP_PORT       8554
#define HTTP_PORT       8080
#define JPEG_MAX        (512 * 1024)

#define ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))
#define NV12_SIZE       ALIGN_UP(WIDTH * HEIGHT * 3 / 2, 0x1000)
#define RAW_SIZE        ALIGN_UP(WIDTH * HEIGHT * 2, 0x400)
#define STREAM_SIZE     ALIGN_UP(WIDTH * HEIGHT / 2, 0x1000)   /* as kdmedia CreateVencVBPool */

#define H264_CHN(n)     (n)
#define JPEG_CHN(n)     (2 + (n))

static const k_vicap_sensor_type slot_sensor[NCAM] = {
    OV5647_MIPI_CSI0_1280X960_45FPS_10BIT_LINEAR,   /* CAM2, J4, CSI0 -> VICAP dev 0 */
    OV5647_MIPI_CSI2_1280X960_45FPS_10BIT_LINEAR,   /* CAM1, J5, CSI2 -> VICAP dev 1 */
};
static const char *session_name[NCAM] = { "cam0", "cam1" };

/* latest JPEG per camera, produced by the cap thread, consumed by HTTP clients */
struct jpeg_slot {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    unsigned char *buf;
    size_t len;
    unsigned seq;          /* bumps on every new JPEG */
    int want;              /* >0: snapshot requests + MJPEG clients waiting */
};

static struct jpeg_slot jslot[NCAM];
static k_u32 stream_pool[4] = { VB_INVALID_POOLID, VB_INVALID_POOLID, VB_INVALID_POOLID,
                                VB_INVALID_POOLID };
static volatile int g_run = 1;
static volatile int g_want_idr[NCAM];
static pthread_t cap_tid[NCAM], venc_tid[NCAM], http_tid;

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

static int venc_create(k_u32 chn, k_payload_type type, k_u32 blocks)
{
    k_vb_pool_config pc;
    memset(&pc, 0, sizeof(pc));
    pc.blk_cnt = blocks;
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
    a.venc_attr.type = type;
    a.venc_attr.pic_width = WIDTH;
    a.venc_attr.pic_height = HEIGHT;
    if (type == K_PT_JPEG) {
        a.rc_attr.rc_mode = K_VENC_RC_MODE_MJPEG_FIXQP;
        a.rc_attr.mjpeg_fixqp.src_frame_rate = FPS;
        a.rc_attr.mjpeg_fixqp.dst_frame_rate = FPS;
        a.rc_attr.mjpeg_fixqp.q_factor = JPEG_QFACTOR;
    } else {
        a.venc_attr.profile = type == K_PT_H265 ? VENC_PROFILE_H265_MAIN : VENC_PROFILE_H264_MAIN;
        a.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
        a.rc_attr.cbr.gop = 2 * FPS;    /* IDR every 2 s; new RTSP clients also request one */
        a.rc_attr.cbr.src_frame_rate = FPS;
        a.rc_attr.cbr.dst_frame_rate = FPS;
        a.rc_attr.cbr.bit_rate = H264_KBPS;
    }
    if (kd_mpi_venc_create_chn(chn, &a)) {
        fprintf(stderr, "venc %u: create_chn failed\n", chn);
        return -1;
    }
    if (type != K_PT_JPEG && kd_mpi_venc_enable_idr(chn, K_TRUE))
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

/* Pull one encoded frame (all its packs) from a VENC channel.
 * sink == NULL: copy the concatenated packs into out (JPEG). Returns bytes or -1. */
typedef void (*pack_sink)(int cam, const k_u8 *data, k_u32 len, k_u64 pts);

static int venc_pull(k_u32 chn, int cam, pack_sink sink, unsigned char *out, size_t cap,
                     k_s32 timeout_ms)
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
    size_t n = 0;
    for (k_u32 i = 0; i < s.pack_cnt; i++) {
        k_u8 *p = kd_mpi_sys_mmap(s.pack[i].phys_addr, s.pack[i].len);
        if (!p)
            continue;
        if (sink)
            sink(cam, p, s.pack[i].len, s.pack[i].pts);
        else if (n + s.pack[i].len <= cap) {
            memcpy(out + n, p, s.pack[i].len);
            n += s.pack[i].len;
        }
        kd_mpi_sys_munmap(p, s.pack[i].len);
    }
    kd_mpi_venc_release_stream(chn, &s);
    return (int)n;
}

static void rtsp_sink(int cam, const k_u8 *data, k_u32 len, k_u64 pts)
{
    rtsp_glue_send(session_name[cam], data, len, pts);   /* copies; SPS/PPS/IDR parsed inside */
}

static void *venc_thread(void *arg)
{
    int cam = (int)(long)arg;
    while (g_run) {
        if (g_want_idr[cam]) {                            /* new RTSP client: start on an IDR */
            g_want_idr[cam] = 0;
            kd_mpi_venc_request_idr(H264_CHN(cam));
        }
        venc_pull(H264_CHN(cam), cam, rtsp_sink, NULL, 0, 1000);
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

static void jpeg_publish(int cam, k_video_frame_info *f)
{
    static unsigned char tmp[NCAM][JPEG_MAX];
    if (kd_mpi_venc_send_frame(JPEG_CHN(cam), f, 1000))
        return;
    int n = venc_pull(JPEG_CHN(cam), cam, NULL, tmp[cam], JPEG_MAX, 1000);
    if (n <= 0)
        return;
    struct jpeg_slot *s = &jslot[cam];
    pthread_mutex_lock(&s->mu);
    memcpy(s->buf, tmp[cam], (size_t)n);
    s->len = (size_t)n;
    s->seq++;
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mu);
}

static void *cap_thread(void *arg)
{
    int cam = (int)(long)arg;
    unsigned frame = 0;
    const k_vicap_chn jchn = VICAP_CHN_ID_0;
    while (g_run) {
        int want;
        pthread_mutex_lock(&jslot[cam].mu);
        want = jslot[cam].want;
        pthread_mutex_unlock(&jslot[cam].mu);
        k_video_frame_info f;
        memset(&f, 0, sizeof(f));
        if (kd_mpi_vicap_dump_frame((k_vicap_dev)cam, jchn, VICAP_DUMP_YUV, &f, 1000)) {
            fprintf(stderr, "vicap %d: no frame in 1 s\n", cam);
            continue;
        }
        kd_mpi_venc_send_frame(H264_CHN(cam), &f, 1000);
        if (want && (frame % MJPEG_EVERY) == 0)
            jpeg_publish(cam, &f);
        /* sample_uvc_dev_vicap releases right after send_frame: VENC holds its own
         * reference on the VB block (not verifiable in source: libvenc/libvpu are binary) */
        kd_mpi_vicap_dump_release((k_vicap_dev)cam, jchn, &f);
        frame++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ HTTP (snapshot + MJPEG) */

static int send_all(int fd, const void *p, size_t n)
{
    const char *c = p;
    while (n) {
        ssize_t k = send(fd, c, n, 0);
        if (k <= 0)
            return -1;
        c += k;
        n -= (size_t)k;
    }
    return 0;
}

/* wait for a JPEG newer than *seq; copies it into out. Returns length or -1. */
static int jpeg_wait(int cam, unsigned *seq, unsigned char *out, int timeout_s)
{
    struct jpeg_slot *s = &jslot[cam];
    struct timespec ts;
    int n = -1;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_s;
    pthread_mutex_lock(&s->mu);
    while (g_run && s->seq == *seq)
        if (pthread_cond_timedwait(&s->cv, &s->mu, &ts) == ETIMEDOUT)
            break;
    if (s->seq != *seq && s->len) {
        memcpy(out, s->buf, s->len);
        n = (int)s->len;
        *seq = s->seq;
    }
    pthread_mutex_unlock(&s->mu);
    return n;
}

static void jpeg_want(int cam, int delta)
{
    pthread_mutex_lock(&jslot[cam].mu);
    jslot[cam].want += delta;
    pthread_mutex_unlock(&jslot[cam].mu);
}

static void *http_client(void *arg)
{
    int fd = (int)(long)arg;
    char req[512];
    int cam = -1, mjpeg = 0;
    ssize_t r = recv(fd, req, sizeof(req) - 1, 0);
    if (r > 0) {
        req[r] = 0;
        if (!strncmp(req, "GET /snap/0", 11) || !strncmp(req, "GET /snap/1", 11))
            cam = req[10] - '0';
        else if (!strncmp(req, "GET /mjpeg/0", 12) || !strncmp(req, "GET /mjpeg/1", 12))
            cam = req[11] - '0', mjpeg = 1;
    }
    if (cam < 0) {
        const char *nf = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(fd, nf, strlen(nf));
        close(fd);
        return NULL;
    }
    unsigned char *buf = malloc(JPEG_MAX);
    unsigned seq = 0;
    char hdr[160];
    jpeg_want(cam, 1);
    if (buf && !mjpeg) {
        pthread_mutex_lock(&jslot[cam].mu);
        seq = jslot[cam].seq;                 /* want a fresh one, not the cached frame */
        pthread_mutex_unlock(&jslot[cam].mu);
        int n = jpeg_wait(cam, &seq, buf, 3);
        if (n > 0) {
            int h = snprintf(hdr, sizeof(hdr), "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\n"
                             "Content-Length: %d\r\nCache-Control: no-cache\r\n\r\n", n);
            if (!send_all(fd, hdr, (size_t)h))
                send_all(fd, buf, (size_t)n);
        }
    } else if (buf) {
        const char *mh = "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\n"
                         "Content-Type: multipart/x-mixed-replace; boundary=leakcam\r\n\r\n";
        if (!send_all(fd, mh, strlen(mh)))
            while (g_run) {
                int n = jpeg_wait(cam, &seq, buf, 3);
                if (n <= 0)
                    continue;
                int h = snprintf(hdr, sizeof(hdr), "--leakcam\r\nContent-Type: image/jpeg\r\n"
                                 "Content-Length: %d\r\n\r\n", n);
                if (send_all(fd, hdr, (size_t)h) || send_all(fd, buf, (size_t)n) ||
                    send_all(fd, "\r\n", 2))
                    break;                    /* client went away */
            }
    }
    jpeg_want(cam, -1);
    free(buf);
    close(fd);
    return NULL;
}

/* select() with a timeout instead of a blocking accept(): RT-Smart does not wake accept() on
 * close (examples/integrated_poc/smart_ipc/http_server.c header comment) */
static void *http_thread(void *arg)
{
    (void)arg;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in a;
    if (s < 0) {
        perror("http socket");
        return NULL;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(HTTP_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 4)) {
        perror("http bind/listen");
        close(s);
        return NULL;
    }
    printf("http: port %d, /snap/{0,1}.jpg /mjpeg/{0,1}\n", HTTP_PORT);
    while (g_run) {
        fd_set rf;
        struct timeval tv = { 1, 0 };
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        if (select(s + 1, &rf, NULL, NULL, &tv) <= 0)
            continue;
        int c = accept(s, NULL, NULL);
        if (c < 0)
            continue;
        struct timeval rto = { 5, 0 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&at, 32 * 1024);
        if (pthread_create(&t, &at, http_client, (void *)(long)c))
            close(c);
        pthread_attr_destroy(&at);
    }
    close(s);
    return NULL;
}

/* ------------------------------------------------------------------ main */

static void on_sig(int sig) { (void)sig; g_run = 0; }

int main(int argc, char **argv)
{
    int ncam = argc > 1 ? atoi(argv[1]) : NCAM;      /* 1 = camera 0 only */
    if (ncam < 1 || ncam > NCAM)
        ncam = NCAM;
    signal(SIGINT, on_sig);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < NCAM; i++) {
        pthread_mutex_init(&jslot[i].mu, NULL);
        pthread_cond_init(&jslot[i].cv, NULL);
        jslot[i].buf = malloc(JPEG_MAX);
        if (!jslot[i].buf)
            return 1;
    }

    /* 1. VB, 2. VENC (pools attached before create; start before frames arrive) */
    if (vb_setup())
        return 1;
    for (int i = 0; i < ncam; i++)
        if (venc_create(H264_CHN(i), K_PT_H264, H264_STREAM_BLK) ||
            venc_create(JPEG_CHN(i), K_PT_JPEG, JPEG_STREAM_BLK))
            goto out;

    /* 3. RTSP sessions (network must be up for clients, not for Init) */
    if (rtsp_glue_init(RTSP_PORT, on_play, NULL) < 0)
        goto out;
    for (int i = 0; i < ncam; i++)
        if (rtsp_glue_add_session(session_name[i], RTSP_H264) < 0)
            goto out;
    rtsp_glue_start();

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
    pthread_create(&http_tid, NULL, http_thread, NULL);
    for (int i = 0; i < ncam; i++)
        rtsp_glue_print_url(session_name[i]);

    while (g_run)
        sleep(1);

    for (int i = 0; i < NCAM; i++)
        pthread_cond_broadcast(&jslot[i].cv);
    pthread_join(http_tid, NULL);
    for (int i = 0; i < ncam; i++) {
        pthread_join(cap_tid[i], NULL);
        pthread_join(venc_tid[i], NULL);
    }
out:
    g_run = 0;
    for (int i = 0; i < ncam; i++) {
        kd_mpi_vicap_stop_stream((k_vicap_dev)i);
        kd_mpi_vicap_deinit((k_vicap_dev)i);
    }
    rtsp_glue_stop();
    for (int i = 0; i < ncam; i++) {
        venc_destroy(H264_CHN(i));
        venc_destroy(JPEG_CHN(i));
    }
    kd_mpi_venc_close_fd();
    kd_mpi_vb_exit();
    return 0;
}
