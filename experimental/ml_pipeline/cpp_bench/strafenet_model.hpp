#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace strafenet {

enum class ArchType : uint32_t {
    TCN_ResMLP = 0,
    GRU_ResMLP = 1
};

struct ModelDims {
    static constexpr uint32_t SEQ_LEN = 32;
    static constexpr uint32_t D_SEQ = 22;
    static constexpr uint32_t D_CTX = 44;
    static constexpr uint32_t HIDDEN = 128;
    static constexpr uint32_t TRUNK_H = 256;
    static constexpr uint32_t OUT_STEPS = 16;
    static constexpr uint32_t OUT_DIMS = 8;
    static constexpr uint32_t OUT_TOTAL = OUT_STEPS * OUT_DIMS;

    static constexpr uint32_t CTX_W1 = D_CTX * HIDDEN;
    static constexpr uint32_t CTX_B1 = HIDDEN;
    static constexpr uint32_t CTX_W2 = HIDDEN * HIDDEN;
    static constexpr uint32_t CTX_B2 = HIDDEN;
    static constexpr uint32_t CTX_TOTAL = CTX_W1 + CTX_B1 + CTX_W2 + CTX_B2;

    static constexpr uint32_t TCN_W1 = (3 * D_SEQ) * HIDDEN;
    static constexpr uint32_t TCN_B1 = HIDDEN;
    static constexpr uint32_t TCN_W2 = (3 * HIDDEN) * HIDDEN;
    static constexpr uint32_t TCN_B2 = HIDDEN;
    static constexpr uint32_t TCN_W3 = (3 * HIDDEN) * HIDDEN;
    static constexpr uint32_t TCN_B3 = HIDDEN;
    static constexpr uint32_t TCN_BACKBONE_TOTAL = CTX_TOTAL + TCN_W1 + TCN_B1 + TCN_W2 + TCN_B2 + TCN_W3 + TCN_B3;

    static constexpr uint32_t GRU_CONV_W = (3 * D_SEQ) * HIDDEN;
    static constexpr uint32_t GRU_CONV_B = HIDDEN;
    static constexpr uint32_t GRU_W_IH = HIDDEN * (3 * HIDDEN);
    static constexpr uint32_t GRU_B_IH = 3 * HIDDEN;
    static constexpr uint32_t GRU_W_HH = HIDDEN * (3 * HIDDEN);
    static constexpr uint32_t GRU_B_HH = 3 * HIDDEN;
    static constexpr uint32_t GRU_BACKBONE_TOTAL = CTX_TOTAL + GRU_CONV_W + GRU_CONV_B + GRU_W_IH + GRU_B_IH + GRU_W_HH + GRU_B_HH;

    static constexpr uint32_t TR_W1 = TRUNK_H * TRUNK_H;
    static constexpr uint32_t TR_B1 = TRUNK_H;
    static constexpr uint32_t TR_W2 = TRUNK_H * HIDDEN;
    static constexpr uint32_t TR_B2 = HIDDEN;
    static constexpr uint32_t TR_W3 = HIDDEN * HIDDEN;
    static constexpr uint32_t TR_B3 = HIDDEN;
    static constexpr uint32_t TR_W4 = HIDDEN * OUT_TOTAL;
    static constexpr uint32_t TR_B4 = OUT_TOTAL;
    static constexpr uint32_t PLASTIC_TOTAL = TR_W1 + TR_B1 + TR_W2 + TR_B2 + TR_W3 + TR_B3 + TR_W4 + TR_B4;
};

#pragma pack(push, 1)
struct Snv1Header {
    char magic[4];
    uint32_t version;
    uint32_t arch_type;
    uint32_t seq_len;
    uint32_t d_seq;
    uint32_t d_ctx;
    uint32_t hidden;
    uint32_t trunk_hidden;
    uint32_t out_steps;
    uint32_t out_dims;
    uint32_t backbone_param_count;
    uint32_t plastic_param_count;
    uint32_t flags;
    uint32_t reserved[3];
};
#pragma pack(pop)
static_assert(sizeof(Snv1Header) == 64, "Snv1Header must be 64 bytes");

struct StrafeNetWeights {
    ArchType arch = ArchType::TCN_ResMLP;
    std::vector<float> backbone;
    std::vector<float> anchor_theta0;
    std::vector<float> plastic_theta;

    size_t off_ctx_w1 = 0;
    size_t off_ctx_b1 = 0;
    size_t off_ctx_w2 = 0;
    size_t off_ctx_b2 = 0;

    size_t off_tcn_w1 = 0;
    size_t off_tcn_b1 = 0;
    size_t off_tcn_w2 = 0;
    size_t off_tcn_b2 = 0;
    size_t off_tcn_w3 = 0;
    size_t off_tcn_b3 = 0;

    size_t off_gru_conv_w = 0;
    size_t off_gru_conv_b = 0;
    size_t off_gru_w_ih = 0;
    size_t off_gru_b_ih = 0;
    size_t off_gru_w_hh = 0;
    size_t off_gru_b_hh = 0;

    size_t off_tr_w1 = 0;
    size_t off_tr_b1 = 0;
    size_t off_tr_w2 = 0;
    size_t off_tr_b2 = 0;
    size_t off_tr_w3 = 0;
    size_t off_tr_b3 = 0;
    size_t off_tr_w4 = 0;
    size_t off_tr_b4 = 0;

    void compute_offsets() {
        size_t o = 0;
        off_ctx_w1 = o; o += ModelDims::CTX_W1;
        off_ctx_b1 = o; o += ModelDims::CTX_B1;
        off_ctx_w2 = o; o += ModelDims::CTX_W2;
        off_ctx_b2 = o; o += ModelDims::CTX_B2;
        if (arch == ArchType::TCN_ResMLP) {
            off_tcn_w1 = o; o += ModelDims::TCN_W1;
            off_tcn_b1 = o; o += ModelDims::TCN_B1;
            off_tcn_w2 = o; o += ModelDims::TCN_W2;
            off_tcn_b2 = o; o += ModelDims::TCN_B2;
            off_tcn_w3 = o; o += ModelDims::TCN_W3;
            off_tcn_b3 = o; o += ModelDims::TCN_B3;
        } else {
            off_gru_conv_w = o; o += ModelDims::GRU_CONV_W;
            off_gru_conv_b = o; o += ModelDims::GRU_CONV_B;
            off_gru_w_ih = o; o += ModelDims::GRU_W_IH;
            off_gru_b_ih = o; o += ModelDims::GRU_B_IH;
            off_gru_w_hh = o; o += ModelDims::GRU_W_HH;
            off_gru_b_hh = o; o += ModelDims::GRU_B_HH;
        }

        size_t po = 0;
        off_tr_w1 = po; po += ModelDims::TR_W1;
        off_tr_b1 = po; po += ModelDims::TR_B1;
        off_tr_w2 = po; po += ModelDims::TR_W2;
        off_tr_b2 = po; po += ModelDims::TR_B2;
        off_tr_w3 = po; po += ModelDims::TR_W3;
        off_tr_b3 = po; po += ModelDims::TR_B3;
        off_tr_w4 = po; po += ModelDims::TR_W4;
        off_tr_b4 = po; po += ModelDims::TR_B4;
    }

    void init_random(ArchType t, uint32_t seed = 2026u) {
        arch = t;
        compute_offsets();
        size_t bb_count = (t == ArchType::TCN_ResMLP) ? ModelDims::TCN_BACKBONE_TOTAL : ModelDims::GRU_BACKBONE_TOTAL;
        backbone.resize(bb_count);
        anchor_theta0.resize(ModelDims::PLASTIC_TOTAL);
        plastic_theta.resize(ModelDims::PLASTIC_TOTAL);

        uint32_t s = seed;
        auto next_f = [&](float scale) -> float {
            s = s * 1664525u + 1013904223u;
            int32_t v = static_cast<int32_t>(s >> 8) - 8388608;
            return static_cast<float>(v) * (scale / 8388608.0f);
        };
        for (size_t i = 0; i < backbone.size(); ++i) backbone[i] = next_f(0.08f);
        for (size_t i = 0; i < ModelDims::PLASTIC_TOTAL; ++i) {
            anchor_theta0[i] = next_f(0.06f);
            plastic_theta[i] = anchor_theta0[i] + next_f(0.01f);
        }
    }

    bool save_snv1(const std::string& path) const {
        Snv1Header h{};
        h.magic[0] = 'S'; h.magic[1] = 'N'; h.magic[2] = 'V'; h.magic[3] = '1';
        h.version = 1;
        h.arch_type = static_cast<uint32_t>(arch);
        h.seq_len = ModelDims::SEQ_LEN;
        h.d_seq = ModelDims::D_SEQ;
        h.d_ctx = ModelDims::D_CTX;
        h.hidden = ModelDims::HIDDEN;
        h.trunk_hidden = ModelDims::TRUNK_H;
        h.out_steps = ModelDims::OUT_STEPS;
        h.out_dims = ModelDims::OUT_DIMS;
        h.backbone_param_count = static_cast<uint32_t>(backbone.size());
        h.plastic_param_count = static_cast<uint32_t>(plastic_theta.size());
        if (anchor_theta0.size() != plastic_theta.size()) return false;

        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(&h), sizeof(h));
        f.write(reinterpret_cast<const char*>(backbone.data()), static_cast<std::streamsize>(backbone.size() * sizeof(float)));
        f.write(reinterpret_cast<const char*>(anchor_theta0.data()), static_cast<std::streamsize>(anchor_theta0.size() * sizeof(float)));
        f.write(reinterpret_cast<const char*>(plastic_theta.data()), static_cast<std::streamsize>(plastic_theta.size() * sizeof(float)));
        return f.good();
    }

    bool load_snv1(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        Snv1Header h{};
        f.read(reinterpret_cast<char*>(&h), sizeof(h));
        if (!f.good()) return false;
        if (std::memcmp(h.magic, "SNV1", 4) != 0 || h.version != 1) return false;
        if (h.arch_type > 1) return false;
        if (h.seq_len != ModelDims::SEQ_LEN || h.d_seq != ModelDims::D_SEQ || h.d_ctx != ModelDims::D_CTX) return false;
        if (h.hidden != ModelDims::HIDDEN || h.trunk_hidden != ModelDims::TRUNK_H) return false;
        if (h.out_steps != ModelDims::OUT_STEPS || h.out_dims != ModelDims::OUT_DIMS) return false;

        arch = static_cast<ArchType>(h.arch_type);
        uint32_t expected_bb = (arch == ArchType::TCN_ResMLP) ? ModelDims::TCN_BACKBONE_TOTAL : ModelDims::GRU_BACKBONE_TOTAL;
        if (h.backbone_param_count != expected_bb || h.plastic_param_count != ModelDims::PLASTIC_TOTAL) return false;

        compute_offsets();
        backbone.resize(h.backbone_param_count);
        anchor_theta0.resize(h.plastic_param_count);
        plastic_theta.resize(h.plastic_param_count);

        f.read(reinterpret_cast<char*>(backbone.data()), static_cast<std::streamsize>(backbone.size() * sizeof(float)));
        f.read(reinterpret_cast<char*>(anchor_theta0.data()), static_cast<std::streamsize>(anchor_theta0.size() * sizeof(float)));
        f.read(reinterpret_cast<char*>(plastic_theta.data()), static_cast<std::streamsize>(plastic_theta.size() * sizeof(float)));
        return f.good();
    }
};

struct TrainConfig {
    float lr = 3e-4f;
    float beta1 = 0.9f;
    float beta2 = 0.999f;
    float eps = 1e-8f;
    float weight_decay = 1e-4f;
    float lambda_anchor = 1e-2f;
    float max_grad_norm = 1.0f;
};

struct AdamWState {
    uint64_t step = 0;
    uint64_t skipped_nonfinite = 0;
    std::vector<float> m;
    std::vector<float> v;

    void reset() {
        step = 0;
        skipped_nonfinite = 0;
        m.assign(ModelDims::PLASTIC_TOTAL, 0.0f);
        v.assign(ModelDims::PLASTIC_TOTAL, 0.0f);
    }
};

struct TrainStepMetrics {
    float mse_loss = 0.0f;
    float l2_anchor_loss = 0.0f;
    float total_loss = 0.0f;
    float grad_norm = 0.0f;
    bool applied = false;
};

inline float fn_sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

inline float fn_silu(float x) {
    return x * fn_sigmoid(x);
}

inline float fn_silu_deriv(float x) {
    float s = fn_sigmoid(x);
    return s + x * s * (1.0f - s);
}

inline float fn_softplus(float x) {
    float ax = std::fabs(x);
    return std::max(x, 0.0f) + std::log1p(std::exp(-ax));
}

inline void ref_sgemm_bias_skip_act(
    const float* A,
    const float* B,
    const float* bias,
    const float* post_skip,
    float* pre_out,
    float* act_out,
    uint32_t M,
    uint32_t N,
    uint32_t K,
    bool silu
) {
    for (uint32_t m = 0; m < M; ++m) {
        const float* a = A + m * K;
        for (uint32_t n = 0; n < N; ++n) {
            float acc = bias ? bias[n] : 0.0f;
            for (uint32_t k = 0; k < K; ++k) {
                acc += a[k] * B[k * N + n];
            }
            if (pre_out) pre_out[m * N + n] = acc;
            if (act_out) {
                float v = silu ? fn_silu(acc) : acc;
                if (post_skip) v += post_skip[m * N + n];
                act_out[m * N + n] = v;
            }
        }
    }
}

inline void ref_im2col_causal_conv1d(
    const float* in,
    float* cols,
    uint32_t batch,
    uint32_t seq_len,
    uint32_t c_in,
    uint32_t dilation
) {
    uint32_t col_stride = 3 * c_in;
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t t = 0; t < seq_len; ++t) {
            float* dst = cols + (b * seq_len + t) * col_stride;
            for (uint32_t tap = 0; tap < 3; ++tap) {
                int32_t src_t = static_cast<int32_t>(t) - static_cast<int32_t>((2 - tap) * dilation);
                float* d_tap = dst + tap * c_in;
                if (src_t >= 0) {
                    std::memcpy(d_tap, in + (b * seq_len + static_cast<uint32_t>(src_t)) * c_in, c_in * sizeof(float));
                } else {
                    std::memset(d_tap, 0, c_in * sizeof(float));
                }
            }
        }
    }
}

inline void ref_apply_out_activations(const float* raw, float* out, uint32_t batch) {
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t s = 0; s < ModelDims::OUT_STEPS; ++s) {
            for (uint32_t c = 0; c < ModelDims::OUT_DIMS; ++c) {
                size_t idx = (static_cast<size_t>(b) * ModelDims::OUT_STEPS + s) * ModelDims::OUT_DIMS + c;
                float u = raw[idx];
                if (c <= 2) {
                    out[idx] = std::tanh(u);
                } else if (c == 3 || c == 4 || c == 7) {
                    out[idx] = fn_sigmoid(u);
                } else {
                    out[idx] = fn_softplus(u) + 1.0f;
                }
            }
        }
    }
}

struct RefTrunkCache {
    std::vector<float> h_seq;
    std::vector<float> h_ctx;
    std::vector<float> z0;
    std::vector<float> pre1;
    std::vector<float> a1;
    std::vector<float> pre2;
    std::vector<float> a2;
    std::vector<float> pre3;
    std::vector<float> a3;
    std::vector<float> raw_out;
    std::vector<float> out;
};

inline void ref_forward(
    const StrafeNetWeights& w,
    const float* x_seq,
    const float* x_ctx,
    float* out,
    uint32_t batch,
    RefTrunkCache* cache = nullptr
) {
    const float* bb = w.backbone.data();
    const float* pt = w.plastic_theta.data();

    std::vector<float> h_ctx(batch * ModelDims::HIDDEN);
    std::vector<float> ctx1(batch * ModelDims::HIDDEN);
    ref_sgemm_bias_skip_act(x_ctx, bb + w.off_ctx_w1, bb + w.off_ctx_b1, nullptr, nullptr, ctx1.data(), batch, ModelDims::HIDDEN, ModelDims::D_CTX, true);
    ref_sgemm_bias_skip_act(ctx1.data(), bb + w.off_ctx_w2, bb + w.off_ctx_b2, nullptr, nullptr, h_ctx.data(), batch, ModelDims::HIDDEN, ModelDims::HIDDEN, true);

    std::vector<float> h_seq(batch * ModelDims::HIDDEN);
    if (w.arch == ArchType::TCN_ResMLP) {
        std::vector<float> cols0(batch * ModelDims::SEQ_LEN * 3 * ModelDims::D_SEQ);
        std::vector<float> c1(batch * ModelDims::SEQ_LEN * ModelDims::HIDDEN);
        ref_im2col_causal_conv1d(x_seq, cols0.data(), batch, ModelDims::SEQ_LEN, ModelDims::D_SEQ, 1);
        ref_sgemm_bias_skip_act(cols0.data(), bb + w.off_tcn_w1, bb + w.off_tcn_b1, nullptr, nullptr, c1.data(), batch * ModelDims::SEQ_LEN, ModelDims::HIDDEN, 3 * ModelDims::D_SEQ, true);

        std::vector<float> cols1(batch * ModelDims::SEQ_LEN * 3 * ModelDims::HIDDEN);
        std::vector<float> c2(batch * ModelDims::SEQ_LEN * ModelDims::HIDDEN);
        ref_im2col_causal_conv1d(c1.data(), cols1.data(), batch, ModelDims::SEQ_LEN, ModelDims::HIDDEN, 2);
        ref_sgemm_bias_skip_act(cols1.data(), bb + w.off_tcn_w2, bb + w.off_tcn_b2, c1.data(), nullptr, c2.data(), batch * ModelDims::SEQ_LEN, ModelDims::HIDDEN, 3 * ModelDims::HIDDEN, true);

        std::vector<float> c3(batch * ModelDims::SEQ_LEN * ModelDims::HIDDEN);
        ref_im2col_causal_conv1d(c2.data(), cols1.data(), batch, ModelDims::SEQ_LEN, ModelDims::HIDDEN, 4);
        ref_sgemm_bias_skip_act(cols1.data(), bb + w.off_tcn_w3, bb + w.off_tcn_b3, c2.data(), nullptr, c3.data(), batch * ModelDims::SEQ_LEN, ModelDims::HIDDEN, 3 * ModelDims::HIDDEN, true);

        for (uint32_t b = 0; b < batch; ++b) {
            const float* last = c3.data() + (b * ModelDims::SEQ_LEN + (ModelDims::SEQ_LEN - 1)) * ModelDims::HIDDEN;
            std::memcpy(h_seq.data() + b * ModelDims::HIDDEN, last, ModelDims::HIDDEN * sizeof(float));
        }
    } else {
        std::vector<float> cols0(batch * ModelDims::SEQ_LEN * 3 * ModelDims::D_SEQ);
        std::vector<float> c1(batch * ModelDims::SEQ_LEN * ModelDims::HIDDEN);
        ref_im2col_causal_conv1d(x_seq, cols0.data(), batch, ModelDims::SEQ_LEN, ModelDims::D_SEQ, 1);
        ref_sgemm_bias_skip_act(cols0.data(), bb + w.off_gru_conv_w, bb + w.off_gru_conv_b, nullptr, nullptr, c1.data(), batch * ModelDims::SEQ_LEN, ModelDims::HIDDEN, 3 * ModelDims::D_SEQ, true);

        constexpr uint32_t H = ModelDims::HIDDEN;
        constexpr uint32_t H3 = 3 * H;
        std::vector<float> h_cur(batch * H, 0.0f);
        std::vector<float> h_nxt(batch * H, 0.0f);
        std::vector<float> x_t(batch * H);
        std::vector<float> gx(batch * H3);
        std::vector<float> gh(batch * H3);
        for (uint32_t t = 0; t < ModelDims::SEQ_LEN; ++t) {
            for (uint32_t b = 0; b < batch; ++b) {
                std::memcpy(x_t.data() + b * H, c1.data() + (b * ModelDims::SEQ_LEN + t) * H, H * sizeof(float));
            }
            ref_sgemm_bias_skip_act(x_t.data(), bb + w.off_gru_w_ih, bb + w.off_gru_b_ih, nullptr, nullptr, gx.data(), batch, H3, H, false);
            ref_sgemm_bias_skip_act(h_cur.data(), bb + w.off_gru_w_hh, bb + w.off_gru_b_hh, nullptr, nullptr, gh.data(), batch, H3, H, false);
            for (uint32_t b = 0; b < batch; ++b) {
                for (uint32_t i = 0; i < H; ++i) {
                    float z = fn_sigmoid(gx[b * H3 + i] + gh[b * H3 + i]);
                    float r = fn_sigmoid(gx[b * H3 + H + i] + gh[b * H3 + H + i]);
                    float n_g = std::tanh(gx[b * H3 + 2 * H + i] + r * gh[b * H3 + 2 * H + i]);
                    h_nxt[b * H + i] = (1.0f - z) * n_g + z * h_cur[b * H + i];
                }
            }
            h_cur.swap(h_nxt);
        }
        h_seq = std::move(h_cur);
    }

    std::vector<float> z0(batch * ModelDims::TRUNK_H);
    for (uint32_t b = 0; b < batch; ++b) {
        std::memcpy(z0.data() + b * ModelDims::TRUNK_H, h_seq.data() + b * ModelDims::HIDDEN, ModelDims::HIDDEN * sizeof(float));
        std::memcpy(z0.data() + b * ModelDims::TRUNK_H + ModelDims::HIDDEN, h_ctx.data() + b * ModelDims::HIDDEN, ModelDims::HIDDEN * sizeof(float));
    }

    std::vector<float> pre1(batch * ModelDims::TRUNK_H);
    std::vector<float> a1(batch * ModelDims::TRUNK_H);
    std::vector<float> pre2(batch * ModelDims::HIDDEN);
    std::vector<float> a2(batch * ModelDims::HIDDEN);
    std::vector<float> pre3(batch * ModelDims::HIDDEN);
    std::vector<float> a3(batch * ModelDims::HIDDEN);
    std::vector<float> raw_out(batch * ModelDims::OUT_TOTAL);

    ref_sgemm_bias_skip_act(z0.data(), pt + w.off_tr_w1, pt + w.off_tr_b1, nullptr, pre1.data(), a1.data(), batch, ModelDims::TRUNK_H, ModelDims::TRUNK_H, true);
    ref_sgemm_bias_skip_act(a1.data(), pt + w.off_tr_w2, pt + w.off_tr_b2, nullptr, pre2.data(), a2.data(), batch, ModelDims::HIDDEN, ModelDims::TRUNK_H, true);
    ref_sgemm_bias_skip_act(a2.data(), pt + w.off_tr_w3, pt + w.off_tr_b3, nullptr, pre3.data(), a3.data(), batch, ModelDims::HIDDEN, ModelDims::HIDDEN, true);
    ref_sgemm_bias_skip_act(a3.data(), pt + w.off_tr_w4, pt + w.off_tr_b4, nullptr, raw_out.data(), nullptr, batch, ModelDims::OUT_TOTAL, ModelDims::HIDDEN, false);
    ref_apply_out_activations(raw_out.data(), out, batch);

    if (cache) {
        cache->h_seq = std::move(h_seq);
        cache->h_ctx = std::move(h_ctx);
        cache->z0 = std::move(z0);
        cache->pre1 = std::move(pre1);
        cache->a1 = std::move(a1);
        cache->pre2 = std::move(pre2);
        cache->a2 = std::move(a2);
        cache->pre3 = std::move(pre3);
        cache->a3 = std::move(a3);
        cache->raw_out = std::move(raw_out);
        cache->out.assign(out, out + batch * ModelDims::OUT_TOTAL);
    }
}

inline TrainStepMetrics ref_compute_trunk_grads(
    const StrafeNetWeights& w,
    const RefTrunkCache& cache,
    const float* target,
    uint32_t batch,
    float lambda_anchor,
    std::vector<float>& grads_out
) {
    TrainStepMetrics m{};
    grads_out.assign(ModelDims::PLASTIC_TOTAL, 0.0f);
    uint32_t N_elem = batch * ModelDims::OUT_TOTAL;
    float inv_n = 1.0f / static_cast<float>(N_elem);

    std::vector<float> d_raw(N_elem);
    double mse_sum = 0.0;
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t s = 0; s < ModelDims::OUT_STEPS; ++s) {
            for (uint32_t c = 0; c < ModelDims::OUT_DIMS; ++c) {
                uint32_t idx = (b * ModelDims::OUT_STEPS + s) * ModelDims::OUT_DIMS + c;
                float p = cache.out[idx];
                float diff = p - target[idx];
                mse_sum += 0.5 * static_cast<double>(diff) * static_cast<double>(diff);
                float d_p = diff * inv_n;
                if (c <= 2) {
                    d_raw[idx] = d_p * (1.0f - p * p);
                } else if (c == 3 || c == 4 || c == 7) {
                    d_raw[idx] = d_p * p * (1.0f - p);
                } else {
                    d_raw[idx] = d_p * fn_sigmoid(cache.raw_out[idx]);
                }
            }
        }
    }
    m.mse_loss = static_cast<float>(mse_sum * inv_n);

    const float* pt = w.plastic_theta.data();
    float* dw4 = grads_out.data() + w.off_tr_w4;
    float* db4 = grads_out.data() + w.off_tr_b4;
    float* dw3 = grads_out.data() + w.off_tr_w3;
    float* db3 = grads_out.data() + w.off_tr_b3;
    float* dw2 = grads_out.data() + w.off_tr_w2;
    float* db2 = grads_out.data() + w.off_tr_b2;
    float* dw1 = grads_out.data() + w.off_tr_w1;
    float* db1 = grads_out.data() + w.off_tr_b1;

    for (uint32_t i = 0; i < ModelDims::HIDDEN; ++i) {
        for (uint32_t j = 0; j < ModelDims::OUT_TOTAL; ++j) {
            float acc = 0.0f;
            for (uint32_t b = 0; b < batch; ++b) acc += cache.a3[b * ModelDims::HIDDEN + i] * d_raw[b * ModelDims::OUT_TOTAL + j];
            dw4[i * ModelDims::OUT_TOTAL + j] = acc;
        }
    }
    for (uint32_t j = 0; j < ModelDims::OUT_TOTAL; ++j) {
        float acc = 0.0f;
        for (uint32_t b = 0; b < batch; ++b) acc += d_raw[b * ModelDims::OUT_TOTAL + j];
        db4[j] = acc;
    }

    std::vector<float> d_a3(batch * ModelDims::HIDDEN);
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t i = 0; i < ModelDims::HIDDEN; ++i) {
            float acc = 0.0f;
            for (uint32_t j = 0; j < ModelDims::OUT_TOTAL; ++j) acc += d_raw[b * ModelDims::OUT_TOTAL + j] * pt[w.off_tr_w4 + i * ModelDims::OUT_TOTAL + j];
            d_a3[b * ModelDims::HIDDEN + i] = acc;
        }
    }
    std::vector<float> d_pre3(batch * ModelDims::HIDDEN);
    for (uint32_t i = 0; i < batch * ModelDims::HIDDEN; ++i) d_pre3[i] = d_a3[i] * fn_silu_deriv(cache.pre3[i]);

    for (uint32_t i = 0; i < ModelDims::HIDDEN; ++i) {
        for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) {
            float acc = 0.0f;
            for (uint32_t b = 0; b < batch; ++b) acc += cache.a2[b * ModelDims::HIDDEN + i] * d_pre3[b * ModelDims::HIDDEN + j];
            dw3[i * ModelDims::HIDDEN + j] = acc;
        }
    }
    for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) {
        float acc = 0.0f;
        for (uint32_t b = 0; b < batch; ++b) acc += d_pre3[b * ModelDims::HIDDEN + j];
        db3[j] = acc;
    }

    std::vector<float> d_a2(batch * ModelDims::HIDDEN);
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t i = 0; i < ModelDims::HIDDEN; ++i) {
            float acc = 0.0f;
            for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) acc += d_pre3[b * ModelDims::HIDDEN + j] * pt[w.off_tr_w3 + i * ModelDims::HIDDEN + j];
            d_a2[b * ModelDims::HIDDEN + i] = acc;
        }
    }
    std::vector<float> d_pre2(batch * ModelDims::HIDDEN);
    for (uint32_t i = 0; i < batch * ModelDims::HIDDEN; ++i) d_pre2[i] = d_a2[i] * fn_silu_deriv(cache.pre2[i]);

    for (uint32_t i = 0; i < ModelDims::TRUNK_H; ++i) {
        for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) {
            float acc = 0.0f;
            for (uint32_t b = 0; b < batch; ++b) acc += cache.a1[b * ModelDims::TRUNK_H + i] * d_pre2[b * ModelDims::HIDDEN + j];
            dw2[i * ModelDims::HIDDEN + j] = acc;
        }
    }
    for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) {
        float acc = 0.0f;
        for (uint32_t b = 0; b < batch; ++b) acc += d_pre2[b * ModelDims::HIDDEN + j];
        db2[j] = acc;
    }

    std::vector<float> d_a1(batch * ModelDims::TRUNK_H);
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t i = 0; i < ModelDims::TRUNK_H; ++i) {
            float acc = 0.0f;
            for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) acc += d_pre2[b * ModelDims::HIDDEN + j] * pt[w.off_tr_w2 + i * ModelDims::HIDDEN + j];
            d_a1[b * ModelDims::TRUNK_H + i] = acc;
        }
    }
    std::vector<float> d_pre1(batch * ModelDims::TRUNK_H);
    for (uint32_t i = 0; i < batch * ModelDims::TRUNK_H; ++i) d_pre1[i] = d_a1[i] * fn_silu_deriv(cache.pre1[i]);

    for (uint32_t i = 0; i < ModelDims::TRUNK_H; ++i) {
        for (uint32_t j = 0; j < ModelDims::TRUNK_H; ++j) {
            float acc = 0.0f;
            for (uint32_t b = 0; b < batch; ++b) acc += cache.z0[b * ModelDims::TRUNK_H + i] * d_pre1[b * ModelDims::TRUNK_H + j];
            dw1[i * ModelDims::TRUNK_H + j] = acc;
        }
    }
    for (uint32_t j = 0; j < ModelDims::TRUNK_H; ++j) {
        float acc = 0.0f;
        for (uint32_t b = 0; b < batch; ++b) acc += d_pre1[b * ModelDims::TRUNK_H + j];
        db1[j] = acc;
    }

    double l2_sum = 0.0;
    double norm_sq = 0.0;
    for (size_t i = 0; i < ModelDims::PLASTIC_TOTAL; ++i) {
        float diff = w.plastic_theta[i] - w.anchor_theta0[i];
        l2_sum += 0.5 * static_cast<double>(lambda_anchor) * static_cast<double>(diff) * static_cast<double>(diff);
        grads_out[i] += lambda_anchor * diff;
        norm_sq += static_cast<double>(grads_out[i]) * static_cast<double>(grads_out[i]);
    }
    m.l2_anchor_loss = static_cast<float>(l2_sum);
    m.total_loss = m.mse_loss + m.l2_anchor_loss;
    m.grad_norm = static_cast<float>(std::sqrt(norm_sq));
    return m;
}

inline TrainStepMetrics ref_train_step(
    StrafeNetWeights& w,
    AdamWState& opt,
    const float* x_seq,
    const float* x_ctx,
    const float* target,
    uint32_t batch,
    const TrainConfig& cfg
) {
    RefTrunkCache cache;
    std::vector<float> pred(batch * ModelDims::OUT_TOTAL);
    ref_forward(w, x_seq, x_ctx, pred.data(), batch, &cache);

    std::vector<float> grads;
    TrainStepMetrics m = ref_compute_trunk_grads(w, cache, target, batch, cfg.lambda_anchor, grads);

    bool finite_ok = std::isfinite(m.grad_norm);
    for (float g : grads) {
        if (!std::isfinite(g)) {
            finite_ok = false;
            break;
        }
    }
    if (!finite_ok) {
        opt.skipped_nonfinite++;
        m.applied = false;
        return m;
    }

    float clip_scale = (m.grad_norm > cfg.max_grad_norm) ? (cfg.max_grad_norm / (m.grad_norm + 1e-12f)) : 1.0f;
    opt.step++;
    float inv_bc1 = 1.0f / (1.0f - std::pow(cfg.beta1, static_cast<float>(opt.step)));
    float inv_bc2 = 1.0f / (1.0f - std::pow(cfg.beta2, static_cast<float>(opt.step)));
    float one_m_b1 = 1.0f - cfg.beta1;
    float one_m_b2 = 1.0f - cfg.beta2;
    float decay = 1.0f - cfg.lr * cfg.weight_decay;

    for (size_t i = 0; i < ModelDims::PLASTIC_TOTAL; ++i) {
        float g = grads[i] * clip_scale;
        float mi = cfg.beta1 * opt.m[i] + one_m_b1 * g;
        float vi = cfg.beta2 * opt.v[i] + one_m_b2 * g * g;
        opt.m[i] = mi;
        opt.v[i] = vi;
        float m_hat = mi * inv_bc1;
        float v_hat = vi * inv_bc2;
        w.plastic_theta[i] = w.plastic_theta[i] * decay - cfg.lr * m_hat / (std::sqrt(v_hat) + cfg.eps);
    }
    m.applied = true;
    return m;
}

inline float max_abs_diff(const float* a, const float* b, size_t n) {
    float md = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float d = std::fabs(a[i] - b[i]);
        if (d > md) md = d;
    }
    return md;
}

}
