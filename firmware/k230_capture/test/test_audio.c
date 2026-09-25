/* Host tests for leakcam_audio.c: the WAV header bytes, the level meter maths and one whole
 * recording from synthetic frames. The source is included unchanged, its main() renamed; the
 * MPP calls are stand-ins below (headers in test/mpp_stub), the codec device is simulated. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* the codec device does not exist here: its ioctls are recorded instead */
static int test_open(const char *path, int flags, ...);
static int test_ioctl(int fd, unsigned long req, ...);
static int test_close(int fd);
#define open test_open
#define ioctl test_ioctl
#define close test_close
#define main audio_main
#include "../leakcam_audio.c"
#undef main
#undef open
#undef ioctl
#undef close

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---------------------------------------------------------------- stand-ins */

#define NFRAMES 80
static int16_t pcm[NFRAMES][POINTS_PER_FRAME];
static unsigned next_frame, get_fail, seq_gap_at, maps, unmaps, releases;
static k_u32 set_gain = 999;
static int codec_fd_open;

static int test_open(const char *path, int flags, ...)
{
    (void)flags;
    if (strcmp(path, "/dev/acodec_device"))
        return -1;
    codec_fd_open = 1;
    return 42;
}

static int test_ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    void *arg;

    if (fd != 42)
        return -1;
    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (req == k_acodec_set_gain_micl)
        set_gain = *(k_u32 *)arg;
    else if (req == k_acodec_get_gain_micl)
        *(k_u32 *)arg = set_gain;
    else if (req == k_acodec_get_alc_gain_micl || req == k_acodec_get_adcl_volume)
        *(float *)arg = 0;
    return 0;
}

static int test_close(int fd)
{
    if (fd == 42)
        codec_fd_open = 0;
    return 0;
}

k_s32 kd_mpi_vb_set_config(const k_vb_config *c) { (void)c; return K_SUCCESS; }
k_s32 kd_mpi_vb_init(void) { return K_SUCCESS; }
k_s32 kd_mpi_vb_exit(void) { return K_SUCCESS; }
static k_aio_dev_attr attr_seen;
k_s32 kd_mpi_ai_set_pub_attr(k_audio_dev d, const k_aio_dev_attr *a) { (void)d; attr_seen = *a; return K_SUCCESS; }
k_s32 kd_mpi_ai_enable(k_audio_dev d) { (void)d; return K_SUCCESS; }
k_s32 kd_mpi_ai_disable(k_audio_dev d) { (void)d; return K_SUCCESS; }
k_s32 kd_mpi_ai_enable_chn(k_audio_dev d, k_ai_chn c) { (void)d; (void)c; return K_SUCCESS; }
k_s32 kd_mpi_ai_disable_chn(k_audio_dev d, k_ai_chn c) { (void)d; (void)c; return K_SUCCESS; }

k_s32 kd_mpi_ai_get_frame(k_audio_dev d, k_ai_chn c, k_audio_frame *f, k_u32 ms)
{
    (void)d;
    (void)c;
    (void)ms;
    if (get_fail || next_frame >= NFRAMES)
        return -1;
    memset(f, 0, sizeof(*f));
    f->phys_addr = (k_u64)(uintptr_t)pcm[next_frame];
    f->len = FRAME_BYTES;
    /* the driver dropped frames: the sequence jumps by 3 */
    f->seq = next_frame + (seq_gap_at && next_frame >= seq_gap_at ? 2 : 0);
    next_frame++;
    return K_SUCCESS;
}

k_s32 kd_mpi_ai_release_frame(k_audio_dev d, k_ai_chn c, const k_audio_frame *f)
{
    (void)d;
    (void)c;
    (void)f;
    releases++;
    return K_SUCCESS;
}

void *kd_mpi_sys_mmap(k_u64 phys, k_u32 size)
{
    (void)size;
    maps++;
    return (void *)(uintptr_t)phys;
}

k_s32 kd_mpi_sys_munmap(void *p, k_u32 size)
{
    (void)p;
    (void)size;
    unmaps++;
    return K_SUCCESS;
}

/* ---------------------------------------------------------------- helpers */

static char *out_buf;
static size_t out_len;
static FILE *saved_stdout;

static void capture(void)
{
    fflush(stdout);
    saved_stdout = stdout;
    stdout = open_memstream(&out_buf, &out_len);
}

static void release(void)
{
    fclose(stdout);
    stdout = saved_stdout;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* one level_print() line, parsed back */
struct line { unsigned t; double rms, rms_db, pk_db, dc; int peak; unsigned clip; };

static struct line meter(const int16_t *s, uint32_t n, unsigned sec)
{
    level_t l;
    struct line r;

    memset(&l, 0, sizeof(l));
    memset(&r, 0, sizeof(r));
    level_add(&l, s, n);
    capture();
    level_print(&l, sec);
    release();
    if (sscanf(out_buf, "t=%us rms %lf (%lf dBFS) peak %d (%lf dBFS) dc %lf clip %u", &r.t, &r.rms, &r.rms_db,
               &r.peak, &r.pk_db, &r.dc, &r.clip) != 7) {
        printf("FAIL: level line not parsed: %s", out_buf);
        fails++;
    }
    free(out_buf);
    return r;
}

/* ---------------------------------------------------------------- tests */

static void t_wav_header(void)
{
    static const uint8_t want_hdr[36] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
        16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0x3e, 0, 0, 0x00, 0x7d, 0, 0, 2, 0, 16, 0,
    };
    uint8_t h[64];
    FILE *f = tmpfile();

    uint8_t junk[60];

    for (unsigned i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(0xa0 + i);    /* an older, longer header: only 44 bytes change */
    fwrite(junk, 1, sizeof(junk), f);
    CHECK(wav_header(f, 64000) == 0, "wav_header failed");
    rewind(f);
    CHECK(fread(h, 1, sizeof(h), f) == 60, "file length changed");
    CHECK(!memcmp(h, want_hdr, 4) && !memcmp(h + 8, want_hdr + 8, 28), "RIFF/fmt chunk bytes");
    CHECK(le32(h + 4) == 36 + 64000 && le32(h + 24) == 16000 && le32(h + 28) == 32000 && le16(h + 32) == 2 &&
          le16(h + 34) == 16, "RIFF size / rate / byte rate / align / bits");
    CHECK(!memcmp(h + 36, "data", 4) && le32(h + 40) == 64000, "data chunk");
    CHECK(!memcmp(h + 44, junk + 44, 16), "header wrote past 44 bytes");
    fclose(f);
}

static void t_levels(void)
{
    static int16_t s[POINTS_PER_FRAME];
    struct line r;
    const uint32_t n = POINTS_PER_FRAME;

    memset(s, 0, sizeof(s));
    r = meter(s, n, 1);
    CHECK(r.t == 1 && r.rms == 0 && r.rms_db == -120 && r.peak == 0 && r.pk_db == -120 && r.dc == 0 && r.clip == 0,
          "silence: rms %.1f (%.1f) peak %d (%.1f)", r.rms, r.rms_db, r.peak, r.pk_db);

    for (uint32_t i = 0; i < n; i++)
        s[i] = i & 1 ? -32767 : 32767;
    r = meter(s, n, 2);
    CHECK(fabs(r.rms - 32767) < 0.1 && fabs(r.rms_db) < 0.05 && r.peak == 32767 && r.clip == n && fabs(r.dc) < 0.05,
          "full-scale square: rms %.1f (%.2f dBFS) peak %d clip %u", r.rms, r.rms_db, r.peak, r.clip);

    for (uint32_t i = 0; i < n; i++)
        s[i] = -32768;
    r = meter(s, n, 3);
    CHECK(r.rms == 0 && r.rms_db == -120 && r.peak == 32768 && r.pk_db == 0 && r.dc == -32768 && r.clip == n,
          "negative full-scale DC: rms %.1f peak %d (%.1f) dc %.1f clip %u", r.rms, r.peak, r.pk_db, r.dc, r.clip);

    /* 1 kHz sine, half scale: rms = A / sqrt(2), -9.03 dBFS; peak -6.02 dBFS; 40 whole periods */
    for (uint32_t i = 0; i < n; i++)
        s[i] = (int16_t)lround(16384 * sin(2 * M_PI * 1000 * i / SAMPLE_RATE));
    r = meter(s, n, 4);
    CHECK(fabs(r.rms - 16384 / sqrt(2)) < 1 && fabs(r.rms_db + 9.03) < 0.05 && r.peak == 16384 &&
          fabs(r.pk_db + 6.02) < 0.05 && fabs(r.dc) < 0.1 && r.clip == 0,
          "half-scale sine: rms %.1f (%.2f dBFS) peak %d (%.2f dBFS) dc %.1f", r.rms, r.rms_db, r.peak, r.pk_db, r.dc);

    /* the codec's input has no high-pass here: DC is reported and kept out of the RMS */
    for (uint32_t i = 0; i < n; i++)
        s[i] = (int16_t)lround(500 + 1000 * sin(2 * M_PI * 500 * i / SAMPLE_RATE));
    r = meter(s, n, 5);
    CHECK(fabs(r.dc - 500) < 0.1 && fabs(r.rms - 1000 / sqrt(2)) < 1 && r.peak == 1500,
          "sine on DC: dc %.1f rms %.1f peak %d", r.dc, r.rms, r.peak);

    /* one clipped sample in a quiet second */
    memset(s, 0, sizeof(s));
    s[100] = 32767;
    s[200] = -32767;
    s[300] = 32766;
    r = meter(s, n, 6);
    CHECK(r.clip == 2 && r.peak == 32767, "clip count %u peak %d", r.clip, r.peak);

    /* sums across frames: 25 frames of the same tone give the same numbers as one */
    level_t l;
    memset(&l, 0, sizeof(l));
    for (uint32_t i = 0; i < n; i++)
        s[i] = (int16_t)lround(8000 * sin(2 * M_PI * 1000 * i / SAMPLE_RATE));
    for (int k = 0; k < FRAMES_PER_SEC; k++)
        level_add(&l, s, n);
    CHECK(l.n == FRAMES_PER_SEC * n && fabs(sqrt(l.sum2 / l.n) - 8000 / sqrt(2)) < 1, "accumulated rms");
}

static int run(char **argv, int argc)
{
    optind = 0;    /* glibc: restart getopt */
    return audio_main(argc, argv);
}

static void t_record(void)
{
    char path[] = "/tmp/leakcam_audio_test_XXXXXX";
    int fd = mkstemp(path), rc;
    uint8_t h[44];

    CHECK(fd >= 0, "mkstemp");
    close(fd);
    for (int k = 0; k < NFRAMES; k++)
        for (int i = 0; i < POINTS_PER_FRAME; i++)
            pcm[k][i] = (int16_t)lround(8000 * sin(2 * M_PI * 1000 * (k * POINTS_PER_FRAME + i) / SAMPLE_RATE));

    /* 2 s after 80 ms of settling, with a 2-frame gap in the driver's sequence; the
     * 8000-peak tone has rms 8000 / sqrt 2 = 5657, 20 log10(5657 / 32768) = -15.3 dBFS */
    next_frame = 0;
    seq_gap_at = 30;
    char *argv[] = { "leakcam_audio", "-d", "2", "-s", "80", "-g", "20", "-o", path, NULL };
    capture();
    rc = run(argv, 9);
    release();
    CHECK(rc == 0, "recording failed (%d): %s", rc, out_buf);
    CHECK(set_gain == 20 && !codec_fd_open, "codec gain %u, device closed %d", set_gain, !codec_fd_open);
    CHECK(attr_seen.kd_audio_attr.i2s_attr.sample_rate == 16000 &&
          attr_seen.kd_audio_attr.i2s_attr.mono_channel == KD_I2S_IN_MONO_LEFT_CHANNEL &&
          attr_seen.kd_audio_attr.i2s_attr.point_num_per_frame == 640, "audio input attributes");
    CHECK(maps == unmaps && releases == next_frame && next_frame == 52, "frames: %u taken, %u released, %u/%u mapped",
          next_frame, releases, maps, unmaps);
    CHECK(strstr(out_buf, "t=  1s  rms  5656.8 ( -15.3 dBFS)  peak  8000 ( -12.2 dBFS)  dc     0.0  clip 0") &&
          strstr(out_buf, "t=  2s") && !strstr(out_buf, "t=  3s"), "level lines:\n%s", out_buf);
    CHECK(strstr(out_buf, "warning: 2 frames lost") != NULL, "sequence gap not reported:\n%s", out_buf);
    free(out_buf);

    FILE *f = fopen(path, "rb");
    struct stat st;
    CHECK(f && stat(path, &st) == 0 && st.st_size == 44 + 50 * FRAME_BYTES, "WAV size %ld", (long)st.st_size);
    CHECK(f && fread(h, 1, 44, f) == 44 && le32(h + 40) == 50 * FRAME_BYTES && le32(h + 4) == 36 + 50 * FRAME_BYTES,
          "WAV header sizes after the recording");
    int same = 1;
    for (int k = 2; f && k < 52; k++) {
        int16_t b[POINTS_PER_FRAME];
        same &= fread(b, 1, FRAME_BYTES, f) == FRAME_BYTES && !memcmp(b, pcm[k], FRAME_BYTES);
    }
    CHECK(same, "PCM differs from frames 2..51 (the first two are the settling skip)");
    if (f)
        fclose(f);

    /* no frames at all: gives up after 20 timeouts, the header is still valid */
    next_frame = 0;
    get_fail = 1;
    capture();
    rc = run(argv, 9);
    release();
    get_fail = 0;
    CHECK(rc == 1 && strstr(out_buf, "giving up"), "no-frames run: rc %d", rc);
    free(out_buf);
    f = fopen(path, "rb");
    CHECK(f && fread(h, 1, 44, f) == 44 && le32(h + 40) == 0 && le32(h + 4) == 36, "empty WAV header");
    if (f)
        fclose(f);

    char *bad[] = { "leakcam_audio", "-g", "31", "-o", path, NULL };
    capture();
    rc = run(bad, 5);
    release();
    CHECK(rc == 1 && strstr(out_buf, "usage:"), "gain 31 accepted");
    free(out_buf);
    unlink(path);
}

int main(void)
{
    t_wav_header();
    t_levels();
    t_record();
    if (fails)
        printf("leakcam_audio: FAIL (%d)\n", fails);
    else
        printf("leakcam_audio: WAV header, RMS/dBFS/DC/peak/clip maths and a full recording all pass\n");
    return fails != 0;
}
