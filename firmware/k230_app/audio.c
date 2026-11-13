/*
 * The microphone as Opus (audio.h). Setup as tests/firmware/k230_apps/leakcam_stream.c, proven on rev 1 (test012):
 *   - the AI takes its frames from a VB pool: without one kd_mpi_ai_get_frame() never returns (2026-10-18);
 *   - order as the SDK's sample_audio: pub attr -> enable (the first enable powers the codec up, ~2 s, and resets its
 *     gains) -> enable chn -> gains;
 *   - frames read through a cached view, flushed first: a non-cached one read 64-byte lines of an older frame (clicks
 *     on 32-sample boundaries).
 * Opus: 24 kbit/s, complexity 3 (a few % of a core), the "music" signal (hiss and ticks, not speech), no DTX (a quiet
 * leak would count as silence and be dropped).
 */
#include "audio.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "k_type.h"
#include "k_audio_comm.h"
#include "k_ai_comm.h"
#include "k_acodec_comm.h"
#include "k_vb_comm.h"
#include "mpi_ai_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "opus.h"

#include "leakcam.h"

#define FRAME       (AUDIO_RATE * AUDIO_MS / 1000)   /* 640 samples */
#define QUEUE       10                  /* AI frames the driver holds: 400 ms (a thread of its own reads them) */
#define OPUS_KBPS   24
#define OPUS_EFFORT 3
#define MIC_PGA_DB  30                  /* mic PGA: 0, 6, 20 or 30 dB */
#define MIC_ALC_DB  24.0f               /* ALC analog gain, -18..28.5 dB: all the gain before the ADC (test012) */
#define SETTLE_MS   2000                /* the codec's input charging after the first enable (test012) */
#define AI_DEV      0                   /* the inner codec, behind I2S device 0 */
#define AI_CHN      0

static OpusEncoder *enc;
static k_u32 pool_id = VB_INVALID_POOLID;
static pthread_t thread;
static volatile bool running;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static audio_fn sink;
static void *sink_ctx;

void audio_sink(audio_fn fn, void *ctx)
{
    pthread_mutex_lock(&mu);
    sink = fn;
    sink_ctx = ctx;
    pthread_mutex_unlock(&mu);
}

static uint64_t mono_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static void *run(void *arg)
{
    (void)arg;
    static opus_int16 pcm[FRAME];
    static uint8_t pkt[1000];
    uint64_t start = mono_us(), packets = 0;
    while (running) {
        k_audio_frame fr;
        if (kd_mpi_ai_get_frame(AI_DEV, AI_CHN, &fr, 200))
            continue;
        void *p = kd_mpi_sys_mmap_cached(fr.phys_addr, fr.len);
        if (p)
            kd_mpi_sys_mmz_flush_cache(fr.phys_addr, p, fr.len);
        bool whole = p && fr.len == sizeof(pcm);
        if (whole)
            memcpy(pcm, p, sizeof(pcm));
        if (p)
            kd_mpi_sys_munmap(p, fr.len);
        uint64_t us = fr.time_stamp;
        kd_mpi_ai_release_frame(AI_DEV, AI_CHN, &fr);
        if (!whole || mono_us() - start < SETTLE_MS * 1000u)
            continue;
        int n = opus_encode(enc, pcm, FRAME, pkt, sizeof(pkt));
        if (n <= 0)
            continue;
        if (!packets++)                 /* the frames' clock against ours: the videos' pts are the cameras' */
            logmsg("audio: first packet %d B, frame time %llu us, monotonic %llu us", n, (unsigned long long)us,
                   (unsigned long long)mono_us());
        pthread_mutex_lock(&mu);
        if (sink)
            sink(pkt, (size_t)n, us, sink_ctx);
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

void audio_close(void)
{
    if (running) {
        running = false;
        pthread_join(thread, NULL);
    }
    kd_mpi_ai_disable_chn(AI_DEV, AI_CHN);
    kd_mpi_ai_disable(AI_DEV);
    if (enc)
        opus_encoder_destroy(enc);
    enc = NULL;
    if (pool_id != VB_INVALID_POOLID)
        kd_mpi_vb_destory_pool(pool_id);
    pool_id = VB_INVALID_POOLID;
}

int audio_open(void)
{
    if (running)
        return 0;
    k_vb_pool_config pool;
    memset(&pool, 0, sizeof(pool));
    pool.blk_cnt = QUEUE + 2;           /* as the SDK's sample_audio: a frame x 4 B x 2, 4 KiB aligned, no-cache */
    pool.blk_size = VB_ALIGN_UP(FRAME * 4 * 2, 4096);
    pool.mode = VB_REMAP_MODE_NOCACHE;
    pool_id = kd_mpi_vb_create_pool(&pool);
    if (pool_id == VB_INVALID_POOLID) {
        logmsg("audio: VB pool failed");
        return -1;
    }
    k_aio_dev_attr a;
    memset(&a, 0, sizeof(a));
    a.audio_type = KD_AUDIO_INPUT_TYPE_I2S;
    a.avsync = K_FALSE;
    a.kd_audio_attr.i2s_attr.sample_rate = AUDIO_RATE;
    a.kd_audio_attr.i2s_attr.bit_width = KD_AUDIO_BIT_WIDTH_16;
    a.kd_audio_attr.i2s_attr.chn_cnt = 2;                     /* the SDK sample: always 2 on I2S */
    a.kd_audio_attr.i2s_attr.snd_mode = KD_AUDIO_SOUND_MODE_MONO;
    a.kd_audio_attr.i2s_attr.mono_channel = KD_I2S_IN_MONO_LEFT_CHANNEL;   /* the SDK names the EVB's jack: the mic */
    a.kd_audio_attr.i2s_attr.i2s_mode = K_STANDARD_MODE;
    a.kd_audio_attr.i2s_attr.frame_num = QUEUE;
    a.kd_audio_attr.i2s_attr.point_num_per_frame = FRAME;
    a.kd_audio_attr.i2s_attr.i2s_type = K_AIO_I2STYPE_INNERCODEC;
    if (kd_mpi_ai_set_pub_attr(AI_DEV, &a) || kd_mpi_ai_enable(AI_DEV) || kd_mpi_ai_enable_chn(AI_DEV, AI_CHN)) {
        logmsg("audio: AI enable failed");
        audio_close();
        return -1;
    }
    int fd = open("/dev/acodec_device", O_RDWR);
    k_u32 pga = MIC_PGA_DB;
    float alc = MIC_ALC_DB;
    if (fd < 0 || ioctl(fd, k_acodec_set_gain_micl, &pga) || ioctl(fd, k_acodec_set_alc_gain_micl, &alc))
        logmsg("audio: mic gain not set");
    if (fd >= 0)
        close(fd);
    int err;
    enc = opus_encoder_create(AUDIO_RATE, 1, OPUS_APPLICATION_AUDIO, &err);
    if (!enc) {
        logmsg("audio: Opus: %s", opus_strerror(err));
        audio_close();
        return -1;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(OPUS_KBPS * 1000));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(OPUS_EFFORT));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    running = true;
    if (pthread_create(&thread, NULL, run, NULL)) {
        running = false;
        logmsg("audio: thread not started");
        audio_close();
        return -1;
    }
    return 0;
}
