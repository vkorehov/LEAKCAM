/*
 * leakcam_audio - LEAKCAM microphone bench test (RT-Smart, K230D inner codec).
 *
 * Records mono 16 kHz 16-bit PCM from the internal codec LEFT analog input
 * (MICPL/MICNL = U13 MSM381ACB026 through C97/C101) to a WAV file and prints
 * RMS / peak / DC per second so a bench test shows whether the mic works.
 *
 * Call order follows src/rtsmart/examples/mpp/sample_audio/audio_sample.c
 * (audio_sample_vb_init -> start_ai -> record_ai_frames -> stop_ai ->
 * audio_sample_vb_destroy):
 *   kd_mpi_vb_set_config / kd_mpi_vb_init
 *   kd_mpi_ai_set_pub_attr(0, I2S, INNERCODEC, chn_cnt=2, MONO, LEFT)
 *   kd_mpi_ai_enable(0)            (first enable: codec power-up, ~2.1 s,
 *                                   ADC init resets gains, MIC_BIAS on)
 *   kd_mpi_ai_enable_chn(0, 0)
 *   /dev/acodec_device ioctls      (gains; after enable, see above)
 *   loop: kd_mpi_ai_get_frame -> kd_mpi_sys_mmap -> use ->
 *         kd_mpi_sys_munmap -> kd_mpi_ai_release_frame
 *   kd_mpi_ai_disable_chn / kd_mpi_ai_disable / kd_mpi_vb_exit
 *
 * Usage: leakcam_audio [-d sec] [-o file.wav] [-g 0|6|20|30] [-a alc_db]
 *                      [-v adc_db] [-s skip_ms] [-r]
 *
 * The codec's LEFT input is the mic: the SDK's names come from the EVB, where "right" is the
 * on-board mic and "left" the headset jack (KD_I2S_IN_MONO_LEFT_CHANNEL, commented "hp input").
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
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

#define SAMPLE_RATE      16000
#define FRAMES_PER_SEC   25                              /* as the SDK sample */
#define POINTS_PER_FRAME (SAMPLE_RATE / FRAMES_PER_SEC)  /* 640 samples, 40 ms */
#define FRAME_BYTES      (POINTS_PER_FRAME * 2)          /* mono s16 */
#define AI_QUEUE_FRAMES  50        /* K_MAX_AUDIO_FRAME_NUM: 2 s of SD-card slack */
#define GET_TIMEOUT_MS   100
#define AI_DEV           0         /* AI_DEV_I2S (inner codec is behind I2S) */
#define AI_CHN           0
#define OUT_DIR          "/sdcard/leakcam"
#define CODEC_REG_BASE   0x9140E000ULL   /* inner codec (rt_ioremap in audio_codec.o) */

static volatile sig_atomic_t g_stop;
static void on_signal(int s) { (void)s; g_stop = 1; }

/* ---- WAV ---------------------------------------------------------------- */
static void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put_le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static int wav_header(FILE *f, uint32_t data_bytes)
{
    uint8_t h[44];
    memcpy(h, "RIFF", 4);      put_le32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le32(h + 16, 16);      put_le16(h + 20, 1);          /* PCM */
    put_le16(h + 22, 1);       put_le32(h + 24, SAMPLE_RATE); /* mono */
    put_le32(h + 28, SAMPLE_RATE * 2);
    put_le16(h + 32, 2);       put_le16(h + 34, 16);
    memcpy(h + 36, "data", 4); put_le32(h + 40, data_bytes);
    if (fseek(f, 0, SEEK_SET) != 0) return -1;
    return fwrite(h, 1, sizeof h, f) == sizeof h ? 0 : -1;
}

/* ---- level meter -------------------------------------------------------- */
typedef struct { double sum, sum2; int32_t peak; uint32_t n, clip; } level_t;

static void level_add(level_t *l, const int16_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        int32_t v = s[i], a = v < 0 ? -v : v;
        l->sum += v; l->sum2 += (double)v * v;
        if (a > l->peak) l->peak = a;
        if (a >= 32767) l->clip++;
    }
    l->n += n;
}

static void level_print(const level_t *l, unsigned sec)
{
    double mean = l->sum / l->n;
    double ac = l->sum2 / l->n - mean * mean;         /* DC removed: the codec has no HPF here */
    double rms = sqrt(ac > 0 ? ac : 0);
    double dbfs = rms > 0 ? 20.0 * log10(rms / 32768.0) : -120.0;
    double pk = l->peak > 0 ? 20.0 * log10(l->peak / 32768.0) : -120.0;
    printf("t=%3us  rms %7.1f (%6.1f dBFS)  peak %5d (%6.1f dBFS)  dc %7.1f  clip %u\n",
           sec, rms, dbfs, (int)l->peak, pk, mean, l->clip);
}

/* ---- codec controls (/dev/acodec_device, k_acodec_comm.h) --------------- */
static void codec_setup(int gain_db, float alc_db, float adc_db, int set_alc, int set_adc)
{
    int fd = open("/dev/acodec_device", O_RDWR);
    if (fd < 0) { perror("open /dev/acodec_device"); return; }
    k_u32 g = (k_u32)gain_db;       /* driver maps 0 / 1..10 / 11..20 / 21..30 -> 0/6/20/30 dB */
    ioctl(fd, k_acodec_set_gain_micl, &g);
    if (set_alc) ioctl(fd, k_acodec_set_alc_gain_micl, &alc_db);
    if (set_adc) ioctl(fd, k_acodec_set_adcl_volume, &adc_db);

    k_u32 rg = 0; float ra = 0, rv = 0;
    ioctl(fd, k_acodec_get_gain_micl, &rg);
    ioctl(fd, k_acodec_get_alc_gain_micl, &ra);
    ioctl(fd, k_acodec_get_adcl_volume, &rv);
    printf("codec left (MICPL): mic PGA %u dB, ALC %.1f dB, ADC digital %.1f dB\n", rg, ra, rv);
    close(fd);
}

/* Optional (-r): read codec analog regs 0x80 (gain_micbias[2:0], en_micbias[3],
 * en_ibias_adc[5], en_vref[6]) .. 0x9c. Field names from the Linux driver
 * k230_sdk src/little/linux/sound/soc/codecs/inno_k230_reg.h. Whether
 * kd_mpi_sys_mmap accepts a register address is not verified. */
static void codec_regdump(void)
{
    volatile uint32_t *r = kd_mpi_sys_mmap(CODEC_REG_BASE, 0x1000);
    if (!r) { printf("regdump: kd_mpi_sys_mmap(0x%llx) failed\n", CODEC_REG_BASE); return; }
    for (unsigned off = 0x80; off <= 0x9c; off += 4)
        printf("codec[0x%02x] = 0x%02x\n", off, (unsigned)(r[off / 4] & 0xff));
    uint32_t r20 = r[0x80 / 4];
    printf("  gain_micbias=%u en_micbias=%u en_ibias_adc=%u en_vref=%u\n",
           (unsigned)(r20 & 7), (unsigned)((r20 >> 3) & 1), (unsigned)((r20 >> 5) & 1),
           (unsigned)((r20 >> 6) & 1));
    kd_mpi_sys_munmap((void *)r, 0x1000);
}

static void default_path(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    mkdir(OUT_DIR, 0777);
    char base[128];
    snprintf(base, sizeof base, OUT_DIR "/audio-%04d%02d%02d-%02d%02d%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    snprintf(out, n, "%s.wav", base);
    /* no HW RTC on LEAKCAM: the clock may read 1970 on every boot -> never overwrite */
    for (int i = 1; access(out, F_OK) == 0 && i < 1000; i++)
        snprintf(out, n, "%s_%03d.wav", base, i);
}

static void usage(const char *p)
{
    printf("usage: %s [-d sec(10)] [-o file.wav] [-g micgain 0|6|20|30 (30)]\n"
           "       [-a alc_db -18..28.5 step 1.5] [-v adc_db -97..30 step 0.5]\n"
           "       [-s skip_ms (500)] [-r regdump]\n", p);
}

int main(int argc, char **argv)
{
    unsigned seconds = 10, skip_ms = 500;
    int gain_db = 30, set_alc = 0, set_adc = 0, regdump = 0, opt;
    float alc_db = 0, adc_db = 0;
    char path[160] = "";

    while ((opt = getopt(argc, argv, "d:o:g:a:v:s:rh")) != -1) {
        switch (opt) {
        case 'd': seconds = strtoul(optarg, NULL, 0); break;
        case 'o': snprintf(path, sizeof path, "%s", optarg); break;
        case 'g': gain_db = atoi(optarg); break;
        case 'a': alc_db = strtof(optarg, NULL); set_alc = 1; break;
        case 'v': adc_db = strtof(optarg, NULL); set_adc = 1; break;
        case 's': skip_ms = strtoul(optarg, NULL, 0); break;
        case 'r': regdump = 1; break;
        default: usage(argv[0]); return 1;
        }
    }
    if (seconds == 0 || seconds > 3600 || gain_db < 0 || gain_db > 30) { usage(argv[0]); return 1; }
    if (!path[0]) default_path(path, sizeof path);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    FILE *f = fopen(path, "wb");
    if (!f) { printf("open %s: %s\n", path, strerror(errno)); return 1; }
    static char fbuf[64 * 1024];
    setvbuf(f, fbuf, _IOFBF, sizeof fbuf);
    if (wav_header(f, 0) != 0) { printf("write header failed\n"); fclose(f); return 1; }

    int rc = 1, vb_ok = 0, dev_on = 0, chn_on = 0;

    /* audio_sample_vb_init(): the AI driver allocates its own private pool
     * (_i2s_create_private_pool), so no user pool is created here. */
    k_vb_config vb;
    memset(&vb, 0, sizeof vb);
    vb.max_pool_cnt = 64;
    if (kd_mpi_vb_set_config(&vb) == K_SUCCESS && kd_mpi_vb_init() == K_SUCCESS)
        vb_ok = 1;
    else
        printf("warning: VB init failed (already initialised by another app?), continuing\n");

    k_aio_dev_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.audio_type = KD_AUDIO_INPUT_TYPE_I2S;
    attr.avsync = K_FALSE;
    attr.kd_audio_attr.i2s_attr.sample_rate = SAMPLE_RATE;
    attr.kd_audio_attr.i2s_attr.bit_width = KD_AUDIO_BIT_WIDTH_16;
    attr.kd_audio_attr.i2s_attr.chn_cnt = 2;                 /* sample: always 2 for I2S */
    attr.kd_audio_attr.i2s_attr.snd_mode = KD_AUDIO_SOUND_MODE_MONO;
    attr.kd_audio_attr.i2s_attr.mono_channel = KD_I2S_IN_MONO_LEFT_CHANNEL;   /* MICPL */
    attr.kd_audio_attr.i2s_attr.i2s_mode = K_STANDARD_MODE;
    attr.kd_audio_attr.i2s_attr.frame_num = AI_QUEUE_FRAMES;
    attr.kd_audio_attr.i2s_attr.point_num_per_frame = POINTS_PER_FRAME;
    attr.kd_audio_attr.i2s_attr.i2s_type = K_AIO_I2STYPE_INNERCODEC;

    if (kd_mpi_ai_set_pub_attr(AI_DEV, &attr) != K_SUCCESS) { printf("kd_mpi_ai_set_pub_attr failed\n"); goto out; }
    printf("enabling AI (first run powers the codec up, ~2 s)...\n");
    if (kd_mpi_ai_enable(AI_DEV) != K_SUCCESS) { printf("kd_mpi_ai_enable failed\n"); goto out; }
    dev_on = 1;
    if (kd_mpi_ai_enable_chn(AI_DEV, AI_CHN) != K_SUCCESS) { printf("kd_mpi_ai_enable_chn failed\n"); goto out; }
    chn_on = 1;

    codec_setup(gain_db, alc_db, adc_db, set_alc, set_adc);
    if (regdump) codec_regdump();

    printf("recording %us, %d Hz s16 mono, LEFT (MICPL) input -> %s\n", seconds, SAMPLE_RATE, path);

    const uint32_t skip_frames = skip_ms / (1000 / FRAMES_PER_SEC); /* C97/C101 + MIC_BIAS settling */
    const uint32_t want = seconds * FRAMES_PER_SEC;
    uint32_t got = 0, skipped = 0, lost = 0, timeouts = 0, next_seq = 0, data_bytes = 0;
    int seq_valid = 0;
    level_t lv;
    memset(&lv, 0, sizeof lv);

    rc = 0;
    while (!g_stop && got < want) {
        k_audio_frame fr;
        if (kd_mpi_ai_get_frame(AI_DEV, AI_CHN, &fr, GET_TIMEOUT_MS) != K_SUCCESS) {
            if (++timeouts > 20) { printf("no audio frames for 2 s, giving up\n"); rc = 1; break; }
            continue;
        }
        timeouts = 0;
        if (seq_valid && fr.seq != next_seq) lost += fr.seq - next_seq;
        seq_valid = 1;
        next_seq = fr.seq + 1;

        if (fr.len != FRAME_BYTES)
            printf("warning: frame len %u (expected %u)\n", fr.len, FRAME_BYTES);
        int16_t *pcm = kd_mpi_sys_mmap(fr.phys_addr, fr.len);
        if (!pcm) {
            printf("kd_mpi_sys_mmap failed\n");
            kd_mpi_ai_release_frame(AI_DEV, AI_CHN, &fr);
            rc = 1;
            break;
        }
        if (skipped < skip_frames) {
            skipped++;
        } else {
            if (fwrite(pcm, 1, fr.len, f) != fr.len) { printf("write failed: %s\n", strerror(errno)); rc = 1; }
            data_bytes += fr.len;
            level_add(&lv, pcm, fr.len / 2);
            got++;
            if (got % FRAMES_PER_SEC == 0) {
                level_print(&lv, got / FRAMES_PER_SEC);
                memset(&lv, 0, sizeof lv);
            }
        }
        kd_mpi_sys_munmap(pcm, fr.len);
        kd_mpi_ai_release_frame(AI_DEV, AI_CHN, &fr);
        if (rc) break;
    }
    if (lost) printf("warning: %u frames lost (sequence gaps)\n", lost);

    if (wav_header(f, data_bytes) != 0) { printf("header update failed\n"); rc = 1; }
    printf("%s: %u bytes PCM (%.2f s)\n", path, data_bytes, data_bytes / (2.0 * SAMPLE_RATE));

out:
    if (chn_on) kd_mpi_ai_disable_chn(AI_DEV, AI_CHN);
    if (dev_on) kd_mpi_ai_disable(AI_DEV);
    if (vb_ok) kd_mpi_vb_exit();
    if (fclose(f) != 0) rc = 1;
    return rc;
}
