/*
 * leak_nn: the two-network LEAKCAM path on RT-Smart, image mode (no camera), for the first
 * on-board test of the kmodels compiled in nn_poc/.
 *
 *   leak_nn <embed.kmodel> <leak.kmodel> <ref_luma> <cur_luma> [--ai2d]
 *
 * ref_luma / cur_luma: 8-bit grey images (PGM from `leakcam_capture --pgm`, or any file
 * cv::imread can read), same camera, 1280x960 or 640x480.
 *
 *   1. image quality (imgqual.c) on the imgdiff-reduced 320x240 frame: exposure, clipping,
 *      sharpness ratio vs the reference -> verdict + LED step suggestion;
 *   2. change network: embed.kmodel (uint8 luma 1x240x320x1 -> 1x15x20x192 features) on both
 *      frames, per-cell cosine distance, max over cells -> "changed" if > --thr;
 *   3. leak network: leak.kmodel (float32 1x90x160x4 stack, lib/preprocess.stack() of
 *      ~/leakcam ported to luma) -> P(inoperational), severity.
 *
 * On the device the reference features would be cached on the NAND (15*20*192 int8 = 57 KB, or
 * a trained 64-d projection: 19 KB) instead of re-running the reference frame every wake.
 *
 * Output order of leak.kmodel: [P(inop), severity], checked in the nncase simulator
 * (check_order.py; the TFLite graph order). The sigmoid output can read slightly below 0
 * after PTQ (-0.03): clamp. Not verified on hardware: the program prints both.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <nncase/functional/ai2d/ai2d_builder.h>
#include <nncase/runtime/interpreter.h>
#include <nncase/runtime/runtime_op_utility.h>
#include <nncase/runtime/runtime_tensor.h>

extern "C" {
#include "imgdiff.h"
#include "imgqual.h"
}

using namespace nncase;
using namespace nncase::runtime;
using namespace nncase::runtime::k230;
using namespace nncase::F::k230;

namespace {

/* lib/preprocess.py constants (the model's training contract) */
constexpr int WALL_CROP = 120;          /* rows of the 640x480 frame used as the exposure reference */
constexpr int SH = 90, SW = 160, SC = 4;

double now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

template <class T> T *host_ptr(runtime_tensor &t, map_access_t acc)
{
    auto buf = t.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(acc).unwrap().buffer();
    return reinterpret_cast<T *>(buf.data());
}

void load(interpreter &ip, const char *path)
{
    std::ifstream ifs(path, std::ios::binary);
    ip.load_model(ifs).expect("invalid kmodel");
    for (size_t i = 0; i < ip.inputs_size(); i++) {
        auto t = host_runtime_tensor::create(ip.input_desc(i).datatype, ip.input_shape(i), hrt::pool_shared)
                     .expect("cannot create input tensor");
        ip.input_tensor(i, t).expect("cannot set input tensor");
    }
    for (size_t i = 0; i < ip.outputs_size(); i++) {
        auto t = host_runtime_tensor::create(ip.output_desc(i).datatype, ip.output_shape(i), hrt::pool_shared)
                     .expect("cannot create output tensor");
        ip.output_tensor(i, t).expect("cannot set output tensor");
    }
}

/* luma HxW -> embed input 240x320 uint8. cv::resize by default; --ai2d uses the 2D engine
 * (single-channel NCHW through ai2d is not verified; the SDK examples only use 3 channels). */
void embed_input(interpreter &ip, const cv::Mat &luma, bool use_ai2d)
{
    runtime_tensor in = ip.input_tensor(0).expect("no input");
    if (!use_ai2d) {
        cv::Mat small;
        cv::resize(luma, small, cv::Size(320, 240), 0, 0, cv::INTER_AREA);
        memcpy(host_ptr<uint8_t>(in, map_access_::map_write), small.data, 320 * 240);
        hrt::sync(in, sync_op_t::sync_write_back, true).expect("sync");
        return;
    }
    dims_t ishape{1, 1, (size_t)luma.rows, (size_t)luma.cols};
    dims_t oshape{1, 1, 240, 320};
    auto src = host_runtime_tensor::create(typecode_t::dt_uint8, ishape, hrt::pool_shared).expect("ai2d in");
    memcpy(host_ptr<uint8_t>(src, map_access_::map_write), luma.data, luma.total());
    hrt::sync(src, sync_op_t::sync_write_back, true).expect("sync");
    ai2d_datatype_t dt{ai2d_format::NCHW_FMT, ai2d_format::NCHW_FMT, typecode_t::dt_uint8, typecode_t::dt_uint8};
    ai2d_crop_param_t crop{false, 0, 0, 0, 0};
    ai2d_shift_param_t shift{false, 0};
    ai2d_pad_param_t pad{false, {{0, 0}, {0, 0}, {0, 0}, {0, 0}}, ai2d_pad_mode::constant, {0}};
    ai2d_resize_param_t rs{true, ai2d_interp_method::tf_bilinear, ai2d_interp_mode::half_pixel};
    ai2d_affine_param_t af{false, ai2d_interp_method::cv2_bilinear, 0, 0, 127, 1, {0.5, 0.1, 0.0, 0.1, 0.5, 0.0}};
    ai2d_builder b(ishape, oshape, dt, crop, shift, pad, rs, af);
    b.build_schedule().expect("ai2d schedule");
    b.invoke(src, in).expect("ai2d invoke");
}

std::vector<float> embed(interpreter &ip, const cv::Mat &luma, bool use_ai2d)
{
    embed_input(ip, luma, use_ai2d);
    ip.run().expect("embed run");
    runtime_tensor o = ip.output_tensor(0).expect("no output");
    size_t n = compute_size(o.shape());
    const float *p = host_ptr<float>(o, map_access_::map_read);
    return std::vector<float>(p, p + n);
}

/* max over cells of (1 - cosine), features NHWC 15x20xC */
float change_distance(const std::vector<float> &a, const std::vector<float> &b, size_t c, float *mean_out)
{
    float worst = 0;
    double sum = 0;
    size_t cells = a.size() / c;
    for (size_t i = 0; i < cells; i++) {
        double ab = 0, aa = 0, bb = 0;
        for (size_t k = 0; k < c; k++) {
            float x = a[i * c + k], y = b[i * c + k];
            ab += x * y, aa += x * x, bb += y * y;
        }
        float d = 1.0f - (float)(ab / (std::sqrt(aa * bb) + 1e-6));
        sum += d;
        worst = d > worst ? d : worst;
    }
    *mean_out = (float)(sum / cells);
    return worst;
}

/* lib/preprocess.stack() on luma: CLAHE(cur), |CLAHE diff|, CLAHE(base), wall-corrected darkening */
void leak_stack(const cv::Mat &cur_in, const cv::Mat &ref_in, float *dst)
{
    cv::Mat cur, ref;
    cv::resize(cur_in, cur, cv::Size(640, 480), 0, 0, cv::INTER_AREA);
    cv::resize(ref_in, ref, cv::Size(640, 480), 0, 0, cv::INTER_AREA);
    auto clahe = cv::createCLAHE(2.5, cv::Size(8, 8));
    cv::Mat a, b, diff;
    clahe->apply(cur, a);
    clahe->apply(ref, b);
    cv::Rect floor(0, WALL_CROP, 640, 480 - WALL_CROP), wall(0, 0, 640, WALL_CROP);
    a = a(floor), b = b(floor);
    cv::absdiff(a, b, diff);
    double wall_delta = cv::mean(cur(wall))[0] - cv::mean(ref(wall))[0];
    cv::Mat dark;
    cv::Mat cf, rf;
    cur(floor).convertTo(cf, CV_32F);
    ref(floor).convertTo(rf, CV_32F);
    dark = cf - rf - wall_delta + 128.0;
    cv::Mat ch[4];
    a.convertTo(ch[0], CV_32F);
    diff.convertTo(ch[1], CV_32F);
    b.convertTo(ch[2], CV_32F);
    ch[3] = cv::min(cv::max(dark, 0.0), 255.0);
    cv::Mat s, out;
    cv::merge(ch, 4, s);
    s *= 1.0 / 255.0;
    cv::resize(s, out, cv::Size(SW, SH), 0, 0, cv::INTER_AREA);
    memcpy(dst, out.ptr<float>(), sizeof(float) * SH * SW * SC);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s embed.kmodel leak.kmodel ref_luma cur_luma [--ai2d] [--thr 0.42]\n", argv[0]);
        return 1;
    }
    bool use_ai2d = false;
    float thr = 0.42f;   /* max over "same" pairs, frozen ImageNet features (README) */
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--ai2d"))
            use_ai2d = true;
        else if (!strcmp(argv[i], "--thr") && i + 1 < argc)
            thr = (float)atof(argv[++i]);
    }
    cv::Mat ref = cv::imread(argv[3], cv::IMREAD_GRAYSCALE), cur = cv::imread(argv[4], cv::IMREAD_GRAYSCALE);
    if (ref.empty() || cur.empty() || ref.size() != cur.size()) {
        fprintf(stderr, "cannot read %s / %s or sizes differ\n", argv[3], argv[4]);
        return 1;
    }

    /* 1. image quality on the imgdiff working size */
    std::vector<uint8_t> rr(IMGDIFF_W * IMGDIFF_H), rc(IMGDIFF_W * IMGDIFF_H);
    if (imgdiff_reduce(ref.data, ref.cols, ref.rows, (int)ref.step, rr.data()) ||
        imgdiff_reduce(cur.data, cur.cols, cur.rows, (int)cur.step, rc.data())) {
        fprintf(stderr, "frame size must be a multiple of %dx%d\n", IMGDIFF_W, IMGDIFF_H);
        return 1;
    }
    struct imgdiff_cfg dcfg;
    imgdiff_default_cfg(&dcfg);
    struct imgqual qr, qc;
    imgqual_measure(rr.data(), IMGDIFF_W, IMGDIFF_H, dcfg.circle_radius, &qr);
    imgqual_measure(rc.data(), IMGDIFF_W, IMGDIFF_H, dcfg.circle_radius, &qc);
    struct imgdiff_result dr;
    imgdiff_compare(rr.data(), rc.data(), &dcfg, &dr);
    printf("{\"quality\":%d,\"p99\":%u,\"sat\":%.4f,\"sharp_ratio\":%.3f,\"led_next_at_100\":%u,"
           "\"imgdiff_changed\":%s,\"imgdiff_blocks\":%d}\n",
           (int)imgqual_judge(&qc, &qr), qc.p99, qc.sat_frac, qc.tenengrad / (qr.tenengrad + 1e-6f),
           imgqual_led_step(&qc, 100), dr.changed ? "true" : "false", dr.changed_blocks);

    /* 2. change network */
    interpreter emb;
    load(emb, argv[1]);
    double t0 = now_ms();
    auto fr = embed(emb, ref, use_ai2d);
    double t1 = now_ms();
    auto fc = embed(emb, cur, use_ai2d);
    size_t c = emb.output_shape(0).back();
    float mean_d, max_d = change_distance(fr, fc, c, &mean_d);
    printf("{\"change_max\":%.3f,\"change_mean\":%.3f,\"changed\":%s,\"embed_ms\":%.1f}\n", max_d, mean_d,
           max_d > thr ? "true" : "false", t1 - t0);

    /* 3. leak network */
    interpreter leak;
    load(leak, argv[2]);
    runtime_tensor in = leak.input_tensor(0).expect("no input");
    double t2 = now_ms();
    leak_stack(cur, ref, host_ptr<float>(in, map_access_::map_write));
    hrt::sync(in, sync_op_t::sync_write_back, true).expect("sync");
    double t3 = now_ms();
    leak.run().expect("leak run");
    double t4 = now_ms();
    float out[2] = {0, 0};
    for (size_t i = 0; i < leak.outputs_size() && i < 2; i++) {
        runtime_tensor o = leak.output_tensor(i).expect("no output");
        out[i] = *host_ptr<float>(o, map_access_::map_read);
    }
    printf("{\"out0_inop\":%.3f,\"out1_severity\":%.3f,\"stack_ms\":%.1f,\"leak_ms\":%.1f}\n", out[0], out[1],
           t3 - t2, t4 - t3);
    return 0;
}
