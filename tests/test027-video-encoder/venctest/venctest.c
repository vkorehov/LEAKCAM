/*
 * TEMPORARY (LEAKCAM tests/test027 H.265 encoder), SDK copy only: copied to src/applications/venctest and listed in
 * apps.mk by hand. DELETE from the SDK after the test.
 *
 *   venctest [channels 1|2] [seconds] [fps, 0 = as fast as the encoder takes them]
 *
 * Feeds VENC channel(s) with generated NV12 frames (1280x960: a gradient that scrolls, a moving square and a
 * noise band, FRAMES distinct frames cycled), H.265 Main, CBR 500 kbit/s, GOP 10 s at 15 fps: the product's
 * settings. Per channel the stream goes to /sdcard/venc<N>.h265 (the first 30 s at most); per second a line:
 * frames in / out, bytes, I frames; at the end fps, kbit/s, IDR spacing, send-to-stream latency (by pts).
 * Max rate (fps 0) keeps 3 frames in flight per channel.
 * Built from the SDK examples (sample_venc: channel and stream; smart_ipc media.cpp: kd_mpi_venc_send_frame
 * with a VB block), no product code.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "k_type.h"
#include "k_module.h"
#include "k_vb_comm.h"
#include "k_venc_comm.h"
#include "k_video_comm.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"

#define W          1280
#define H          960
#define KBPS       500
#define GOP_FPS    15
#define GOP_S      10
#define FRAMES     8                    /* distinct input frames per channel, cycled */
#define NV12       VB_ALIGN_UP(W * H * 3 / 2, 0x1000)
#define STREAM_BLK_SIZE VB_ALIGN_UP(W * H / 2, 0x1000)
#define FILE_LIMIT (4u << 20)          /* per channel, ~65 s at 500 kbit/s: /sdcard holds ~20 MB free */

static int nchn = 1, seconds = 20, fps = 15;
static volatile int running = 1;
static k_u32 frame_pool, stream_pool[2];
static k_vb_blk_handle blk[2][FRAMES];
static k_u64 blk_phys[2][FRAMES];

struct stat_t {
    unsigned in, out, iframes, last_i, max_gap, min_gap;
    unsigned long long bytes, lat_us_sum, lat_us_max;
    unsigned long long sent_us[256];    /* send time by frame number % 256: latency */
};
static struct stat_t st[2];

static unsigned long long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

/* frame f of channel c: luma gradient scrolling 4 px per frame, a 160x160 square crossing, a noise band */
static void fill(k_u8 *y, int c, int f)
{
    unsigned r = 12345u + f * 7919u + c * 104729u;
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++)
            y[j * W + i] = (k_u8)((i + j + f * 4 + c * 64) & 0xff);
    int sx = (f * 40 + c * 300) % (W - 160), sy = (f * 25) % (H - 160);
    for (int j = sy; j < sy + 160; j++)
        memset(y + j * W + sx, c ? 30 : 230, 160);
    for (int j = H - 64; j < H; j++)
        for (int i = 0; i < W; i++)
            r = r * 1103515245u + 12345u, y[j * W + i] = (k_u8)(r >> 24);
    k_u8 *uv = y + W * H;
    for (int j = 0; j < H / 2; j++)
        for (int i = 0; i < W; i += 2)
            uv[j * W + i] = (k_u8)(128 + ((i / 8 + f) & 31)), uv[j * W + i + 1] = (k_u8)(128 - ((j / 8) & 31));
}

static int setup(void)
{
    k_vb_config vb;
    memset(&vb, 0, sizeof(vb));
    vb.max_pool_cnt = 16;
    if (kd_mpi_vb_set_config(&vb) || kd_mpi_vb_init()) {
        printf("venctest: vb init failed\n");
        return -1;
    }
    k_vb_pool_config pc;
    memset(&pc, 0, sizeof(pc));
    pc.blk_cnt = FRAMES * nchn;
    pc.blk_size = NV12;
    pc.mode = VB_REMAP_MODE_NOCACHE;
    frame_pool = kd_mpi_vb_create_pool(&pc);
    if (frame_pool == VB_INVALID_POOLID) {
        printf("venctest: frame pool (%d x %u B) failed\n", FRAMES * nchn, (unsigned)NV12);
        return -1;
    }
    k_u8 *tmp = malloc(NV12);
    for (int c = 0; c < nchn; c++) {
        for (int f = 0; f < FRAMES; f++) {
            blk[c][f] = kd_mpi_vb_get_block(frame_pool, NV12, NULL);
            if (blk[c][f] == VB_INVALID_HANDLE) {
                printf("venctest: get_block failed\n");
                return -1;
            }
            blk_phys[c][f] = kd_mpi_vb_handle_to_phyaddr(blk[c][f]);
            k_u8 *v = kd_mpi_sys_mmap(blk_phys[c][f], NV12);
            fill(tmp, c, f);
            memcpy(v, tmp, W * H * 3 / 2);
            kd_mpi_sys_munmap(v, NV12);
        }
        memset(&pc, 0, sizeof(pc));
        pc.blk_cnt = 4;
        pc.blk_size = STREAM_BLK_SIZE;
        pc.mode = VB_REMAP_MODE_NOCACHE;
        stream_pool[c] = kd_mpi_vb_create_pool(&pc);
        kd_mpi_venc_attach_vb_pool(c, stream_pool[c]);
        k_venc_chn_attr a;
        memset(&a, 0, sizeof(a));
        a.venc_attr.type = K_PT_H265;
        a.venc_attr.pic_width = W;
        a.venc_attr.pic_height = H;
        a.venc_attr.profile = VENC_PROFILE_H265_MAIN;
        a.rc_attr.rc_mode = K_VENC_RC_MODE_CBR;
        a.rc_attr.cbr.gop = GOP_S * GOP_FPS;
        a.rc_attr.cbr.src_frame_rate = GOP_FPS;
        a.rc_attr.cbr.dst_frame_rate = GOP_FPS;
        a.rc_attr.cbr.bit_rate = KBPS;
        if (kd_mpi_venc_create_chn(c, &a) || kd_mpi_venc_start_chn(c)) {
            printf("venctest: venc %d create/start failed\n", c);
            return -1;
        }
    }
    free(tmp);
    return 0;
}

static void *feed(void *arg)
{
    int c = (int)(long)arg;
    unsigned long long t0 = now_us(), period = fps ? 1000000ull / fps : 0;
    for (unsigned n = 0; running; n++) {
        if (period) {
            unsigned long long due = t0 + n * period, t = now_us();
            if (due > t)
                usleep((useconds_t)(due - t));
        } else {
            /* max rate: send_frame does not block on a full input queue, it fails at once (2026-10-21: a
             * feed spinning on it flooded the console and hung the run); keep 3 frames in flight */
            while (running && st[c].in - st[c].out >= 3)
                usleep(500);
        }
        k_video_frame_info vf;
        memset(&vf, 0, sizeof(vf));
        vf.mod_id = K_ID_VO;
        vf.pool_id = frame_pool;
        vf.v_frame.width = W;
        vf.v_frame.height = H;
        vf.v_frame.stride[0] = W;
        vf.v_frame.stride[1] = W;
        vf.v_frame.pixel_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        vf.v_frame.phys_addr[0] = blk_phys[c][n % FRAMES];
        vf.v_frame.phys_addr[1] = blk_phys[c][n % FRAMES] + W * H;
        vf.v_frame.pts = n;
        vf.v_frame.time_ref = n;
        st[c].sent_us[n & 255] = now_us();
        if (kd_mpi_venc_send_frame(c, &vf, -1) == 0)
            st[c].in++;
        else
            usleep(2000);
    }
    return NULL;
}

static void *drain(void *arg)
{
    int c = (int)(long)arg;
    char name[32];
    snprintf(name, sizeof(name), "/sdcard/venc%d.h265", c);
    FILE *fp = fopen(name, "wb");
    unsigned long long written = 0;
    while (running) {
        k_venc_chn_status s;
        k_venc_stream out;
        k_venc_pack packs[8];
        if (kd_mpi_venc_query_status(c, &s))
            continue;
        memset(&out, 0, sizeof(out));
        out.pack_cnt = s.cur_packs > 0 ? (s.cur_packs > 8 ? 8 : s.cur_packs) : 1;
        out.pack = packs;
        if (kd_mpi_venc_get_stream(c, &out, 500))
            continue;
        unsigned long long t = now_us();
        int is_i = 0;
        for (k_u32 i = 0; i < out.pack_cnt; i++) {
            st[c].bytes += out.pack[i].len;
            is_i |= out.pack[i].type == K_VENC_I_FRAME;
            if (fp && written + out.pack[i].len <= FILE_LIMIT) {
                k_u8 *p = kd_mpi_sys_mmap(out.pack[i].phys_addr, out.pack[i].len);
                size_t w = fwrite(p, 1, out.pack[i].len, fp);
                kd_mpi_sys_munmap(p, out.pack[i].len);
                written += out.pack[i].len;
                if (w != out.pack[i].len) {
                    /* a full /sdcard stalled every write and with them the drain: the input queue filled and both
                     * channels stopped at t=100 s (2026-10-30, test028's files left 12 MB) */
                    printf("venctest: ch%d disk full after %llu B, stream no longer saved\n", c, written - out.pack[i].len + w);
                    fclose(fp);
                    fp = NULL;
                }
            }
        }
        unsigned n = st[c].out;
        /* by pts (= frame number at send), not by count: a dropped frame would shift every later one */
        unsigned long long lat = t - st[c].sent_us[out.pack[out.pack_cnt - 1].pts & 255];
        st[c].lat_us_sum += lat;
        if (lat > st[c].lat_us_max)
            st[c].lat_us_max = lat;
        if (is_i) {
            if (st[c].iframes) {
                unsigned gap = n - st[c].last_i;
                if (gap > st[c].max_gap)
                    st[c].max_gap = gap;
                if (!st[c].min_gap || gap < st[c].min_gap)
                    st[c].min_gap = gap;
            }
            st[c].iframes++, st[c].last_i = n;
        }
        st[c].out++;
        kd_mpi_venc_release_stream(c, &out);
    }
    if (fp) {
        fflush(fp);
        fsync(fileno(fp));
        fclose(fp);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    nchn = argc > 1 ? atoi(argv[1]) : 1;
    seconds = argc > 2 ? atoi(argv[2]) : 20;
    fps = argc > 3 ? atoi(argv[3]) : 15;
    if (nchn < 1 || nchn > 2)
        nchn = 1;
    printf("venctest: %d channel(s) H.265 %dx%d CBR %d kbit/s, feed %s%d fps, %d s\n", nchn, W, H, KBPS,
           fps ? "" : "max ", fps, seconds);
    if (setup())
        return 1;
    pthread_t tf[2], td[2];
    for (int c = 0; c < nchn; c++) {
        pthread_create(&td[c], NULL, drain, (void *)(long)c);
        pthread_create(&tf[c], NULL, feed, (void *)(long)c);
    }
    unsigned long long t0 = now_us();
    struct stat_t prev[2];
    memset(prev, 0, sizeof(prev));
    for (int s = 1; s <= seconds; s++) {
        sleep(1);
        for (int c = 0; c < nchn; c++) {
            printf("t=%2d ch%d: in %3u out %3u fps, %6llu B, I %u\n", s, c, st[c].in - prev[c].in,
                   st[c].out - prev[c].out, st[c].bytes - prev[c].bytes, st[c].iframes - prev[c].iframes);
            prev[c] = st[c];
        }
        fflush(stdout);
    }
    double dt = (now_us() - t0) / 1e6;
    running = 0;
    for (int c = 0; c < nchn; c++) {
        pthread_join(tf[c], NULL);
        pthread_join(td[c], NULL);
        kd_mpi_venc_stop_chn(c);
        kd_mpi_venc_detach_vb_pool(c);
        kd_mpi_venc_destroy_chn(c);
        kd_mpi_vb_destory_pool(stream_pool[c]);
        printf("ch%d: %u in, %u out in %.1f s = %.1f fps, %.0f kbit/s, I frames %u (spacing %u-%u), "
               "latency avg %llu max %llu ms\n", c, st[c].in, st[c].out, dt, st[c].out / dt,
               st[c].bytes * 8 / dt / 1000, st[c].iframes, st[c].min_gap, st[c].max_gap,
               st[c].out ? st[c].lat_us_sum / st[c].out / 1000 : 0, st[c].lat_us_max / 1000);
        for (int f = 0; f < FRAMES; f++)
            kd_mpi_vb_release_block(blk[c][f]);
    }
    kd_mpi_vb_destory_pool(frame_pool);
    kd_mpi_vb_exit();
    printf("venctest: done\n");
    return 0;
}
