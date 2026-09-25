/*
 * Host stand-in for the K230 MPP headers leakcam_audio.c includes: the types, enum names and
 * prototypes it uses, as in src/rtsmart/mpp of the k230_rtos_sdk. The functions are
 * implemented by test/test_audio.c. Each MPP header name is a one-line file including this.
 */
#ifndef TEST_MPP_STUB_H
#define TEST_MPP_STUB_H

#include <stdint.h>

typedef uint32_t k_u32;
typedef int32_t k_s32;
typedef uint64_t k_u64;
typedef int k_bool;
#define K_SUCCESS 0
#define K_FALSE   0
#define K_TRUE    1

typedef k_u32 k_audio_dev;
typedef k_u32 k_ai_chn;

typedef enum { KD_AUDIO_BIT_WIDTH_16 = 0, KD_AUDIO_BIT_WIDTH_24, KD_AUDIO_BIT_WIDTH_32 } k_audio_bit_width;
typedef enum { KD_AUDIO_SOUND_MODE_MONO = 0, KD_AUDIO_SOUND_MODE_STEREO } k_audio_snd_mode;
typedef enum { KD_AUDIO_INPUT_TYPE_I2S = 0, KD_AUDIO_INPUT_TYPE_PDM } k_audio_type;
typedef enum { KD_I2S_IN_MONO_RIGHT_CHANNEL = 0, KD_I2S_IN_MONO_LEFT_CHANNEL } k_i2s_in_mono_channel;
typedef enum { K_STANDARD_MODE = 1, K_RIGHT_JUSTIFYING_MODE, K_LEFT_JUSTIFYING_MODE } k_i2s_work_mode;
typedef enum { K_AIO_I2STYPE_INNERCODEC = 0, K_AIO_I2STYPE_EXTERN } k_aio_i2s_type;

typedef struct {
    k_u32 sample_rate;
    k_audio_bit_width bit_width;
    k_u32 chn_cnt;
    k_audio_snd_mode snd_mode;
    k_i2s_in_mono_channel mono_channel;
    k_i2s_work_mode i2s_mode;
    k_u32 frame_num;
    k_u32 point_num_per_frame;
    k_aio_i2s_type i2s_type;
} k_audio_i2s_attr;

typedef struct {
    k_audio_type audio_type;
    k_bool avsync;
    union { k_audio_i2s_attr i2s_attr; } kd_audio_attr;
} k_aio_dev_attr;

typedef struct {
    k_audio_bit_width bit_width;
    k_audio_snd_mode snd_mode;
    void *virt_addr;
    k_u64 phys_addr;
    k_u64 time_stamp;
    k_u32 seq;
    k_u32 len;
    k_u32 pool_id;
} k_audio_frame;

typedef struct { k_u32 max_pool_cnt; } k_vb_config;

/* /dev/acodec_device ioctl numbers (k_acodec_comm.h) */
enum {
    k_acodec_set_gain_micl = 10,
    k_acodec_set_adcl_volume = 12,
    k_acodec_set_alc_gain_micl = 14,
    k_acodec_get_gain_micl = 24,
    k_acodec_get_adcl_volume = 26,
    k_acodec_get_alc_gain_micl = 28,
};

k_s32 kd_mpi_ai_set_pub_attr(k_audio_dev ai_dev, const k_aio_dev_attr *attr);
k_s32 kd_mpi_ai_enable(k_audio_dev ai_dev);
k_s32 kd_mpi_ai_disable(k_audio_dev ai_dev);
k_s32 kd_mpi_ai_enable_chn(k_audio_dev ai_dev, k_ai_chn ai_chn);
k_s32 kd_mpi_ai_disable_chn(k_audio_dev ai_dev, k_ai_chn ai_chn);
k_s32 kd_mpi_ai_get_frame(k_audio_dev ai_dev, k_ai_chn ai_chn, k_audio_frame *frame, k_u32 milli_sec);
k_s32 kd_mpi_ai_release_frame(k_audio_dev ai_dev, k_ai_chn ai_chn, const k_audio_frame *frame);
void *kd_mpi_sys_mmap(k_u64 phy_addr, k_u32 size);
k_s32 kd_mpi_sys_munmap(void *virt_addr, k_u32 size);
k_s32 kd_mpi_vb_init(void);
k_s32 kd_mpi_vb_exit(void);
k_s32 kd_mpi_vb_set_config(const k_vb_config *config);

#endif
