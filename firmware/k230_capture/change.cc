/* The change net on the KPU through the nncase runtime (change.h). */
#include <cmath>
#include <cstdio>
#include <cstring>

#include <nncase/runtime/interpreter.h>
#include <nncase/runtime/runtime_op_utility.h>
#include <nncase/runtime/runtime_tensor.h>

#include "change.h"
#include "imgdiff.h"

using namespace nncase;
using namespace nncase::runtime;

namespace {

interpreter *net;

template <class T> T *host_ptr(runtime_tensor &t, map_access_t acc)
{
    auto buf = t.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(acc).unwrap().buffer();
    return reinterpret_cast<T *>(buf.data());
}

/* one frame in, its features (NHWC 15x20x192 float, in the output tensor) out */
const float *features(const uint8_t *luma)
{
    runtime_tensor in = net->input_tensor(0).unwrap();
    memcpy(host_ptr<uint8_t>(in, map_access_::map_write), luma, IMGDIFF_W * IMGDIFF_H);
    if (hrt::sync(in, sync_op_t::sync_write_back, true).is_err() || net->run().is_err())
        return nullptr;
    runtime_tensor out = net->output_tensor(0).unwrap();
    return host_ptr<float>(out, map_access_::map_read);
}

}  // namespace

int change_load(const uint8_t *kmodel, size_t len)
{
    net = new interpreter;
    if (net->load_model({ reinterpret_cast<const gsl::byte *>(kmodel), len }).is_err()) {
        fprintf(stderr, "change net: invalid kmodel\n");
        return -1;
    }
    auto in = host_runtime_tensor::create(net->input_desc(0).datatype, net->input_shape(0), hrt::pool_shared);
    auto out = host_runtime_tensor::create(net->output_desc(0).datatype, net->output_shape(0), hrt::pool_shared);
    if (in.is_err() || out.is_err() || net->input_tensor(0, in.unwrap()).is_err() ||
        net->output_tensor(0, out.unwrap()).is_err()) {
        fprintf(stderr, "change net: cannot create its tensors\n");
        return -1;
    }
    return 0;
}

float change_distance(const uint8_t *ref, const uint8_t *cur)
{
    const size_t c = net->output_shape(0).back(), cells = compute_size(net->output_shape(0)) / c;
    static float *fr = new float[cells * c];
    const float *p = features(ref);
    if (!p)
        return -1;
    memcpy(fr, p, cells * c * sizeof(float));
    const float *fc = features(cur);
    if (!fc)
        return -1;
    float worst = 0;
    for (size_t i = 0; i < cells; i++) {
        float ab = 0, aa = 0, bb = 0;
        for (size_t k = 0; k < c; k++) {
            float x = fr[i * c + k], y = fc[i * c + k];
            ab += x * y, aa += x * x, bb += y * y;
        }
        float d = 1.0f - ab / (std::sqrt(aa * bb) + 1e-6f);
        worst = d > worst ? d : worst;
    }
    return worst;
}
