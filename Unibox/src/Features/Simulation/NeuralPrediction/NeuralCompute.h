#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <DirectML.h>
#include <wrl/client.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <immintrin.h>
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

namespace NeuralCompute {


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

    bool load_snv1_memory(const void* data, size_t size) {
        if (!data || size < sizeof(Snv1Header)) return false;
        Snv1Header h{};
        std::memcpy(&h, data, sizeof(Snv1Header));
        if (std::memcmp(h.magic, "SNV1", 4) != 0 || h.version != 1) return false;
        if (h.arch_type > 1) return false;
        if (h.seq_len != ModelDims::SEQ_LEN || h.d_seq != ModelDims::D_SEQ || h.d_ctx != ModelDims::D_CTX) return false;
        if (h.hidden != ModelDims::HIDDEN || h.trunk_hidden != ModelDims::TRUNK_H) return false;
        if (h.out_steps != ModelDims::OUT_STEPS || h.out_dims != ModelDims::OUT_DIMS) return false;

        arch = static_cast<ArchType>(h.arch_type);
        uint32_t expected_bb = (arch == ArchType::TCN_ResMLP) ? ModelDims::TCN_BACKBONE_TOTAL : ModelDims::GRU_BACKBONE_TOTAL;
        if (h.backbone_param_count != expected_bb || h.plastic_param_count != ModelDims::PLASTIC_TOTAL) return false;

        size_t need = sizeof(Snv1Header) + (static_cast<size_t>(h.backbone_param_count) + 2 * static_cast<size_t>(h.plastic_param_count)) * sizeof(float);
        if (size < need) return false;

        compute_offsets();
        backbone.resize(h.backbone_param_count);
        anchor_theta0.resize(h.plastic_param_count);
        plastic_theta.resize(h.plastic_param_count);

        const uint8_t* ptr = static_cast<const uint8_t*>(data) + sizeof(Snv1Header);
        std::memcpy(backbone.data(), ptr, backbone.size() * sizeof(float));
        ptr += backbone.size() * sizeof(float);
        std::memcpy(anchor_theta0.data(), ptr, anchor_theta0.size() * sizeof(float));
        ptr += anchor_theta0.size() * sizeof(float);
        std::memcpy(plastic_theta.data(), ptr, plastic_theta.size() * sizeof(float));
        return true;
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


namespace strafenet_cpu {

struct CpuFeatures {
    char vendor[13] = {};
    char brand[49] = {};
    bool sse2 = false;
    bool avx = false;
    bool fma = false;
    bool osxsave = false;
    bool avx2 = false;
};

inline CpuFeatures detect_cpu() {
    CpuFeatures c{};
    int info[4] = {};
    __cpuid(info, 0);
    int max_leaf = info[0];
    std::memcpy(c.vendor + 0, &info[1], 4);
    std::memcpy(c.vendor + 4, &info[3], 4);
    std::memcpy(c.vendor + 8, &info[2], 4);
    c.vendor[12] = '\0';

    if (max_leaf >= 1) {
        __cpuid(info, 1);
        c.sse2 = (info[3] & (1 << 26)) != 0;
        c.fma = (info[2] & (1 << 12)) != 0;
        c.osxsave = (info[2] & (1 << 27)) != 0;
        c.avx = (info[2] & (1 << 28)) != 0;
    }
    bool ymm_ok = false;
    if (c.osxsave) {
        ymm_ok = (_xgetbv(0) & 0x6) == 0x6;
    }
    if (max_leaf >= 7 && ymm_ok) {
        __cpuidex(info, 7, 0);
        c.avx2 = (info[1] & (1 << 5)) != 0;
    } else {
        c.avx = false;
        c.fma = false;
    }
    __cpuid(info, static_cast<int>(0x80000000u));
    if (static_cast<uint32_t>(info[0]) >= 0x80000004u) {
        __cpuid(reinterpret_cast<int*>(c.brand + 0), static_cast<int>(0x80000002u));
        __cpuid(reinterpret_cast<int*>(c.brand + 16), static_cast<int>(0x80000003u));
        __cpuid(reinterpret_cast<int*>(c.brand + 32), static_cast<int>(0x80000004u));
        c.brand[48] = '\0';
    }
    return c;
}

inline bool is_avx2_fma_supported() {
    static const bool ok = []() {
        CpuFeatures c = detect_cpu();
        return c.avx2 && c.fma && c.avx;
    }();
    return ok;
}

inline __m256 avx2_exp_ps(__m256 x) {
    x = _mm256_min_ps(x, _mm256_set1_ps(88.3762626647949f));
    x = _mm256_max_ps(x, _mm256_set1_ps(-87.3365447504f));
    __m256 fx = _mm256_round_ps(_mm256_mul_ps(x, _mm256_set1_ps(1.44269504088896341f)), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    __m256 z = _mm256_fnmadd_ps(fx, _mm256_set1_ps(0.693359375f), x);
    z = _mm256_fnmadd_ps(fx, _mm256_set1_ps(-2.12194440e-4f), z);
    __m256 y = _mm256_set1_ps(1.9875691500e-4f);
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(1.3981999507e-3f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(8.3334519073e-3f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(4.1665795894e-2f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(1.6666665459e-1f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(5.0000001201e-1f));
    y = _mm256_fmadd_ps(y, _mm256_mul_ps(z, z), z);
    y = _mm256_add_ps(y, _mm256_set1_ps(1.0f));
    __m256i emm0 = _mm256_slli_epi32(_mm256_add_epi32(_mm256_cvttps_epi32(fx), _mm256_set1_epi32(127)), 23);
    return _mm256_mul_ps(y, _mm256_castsi256_ps(emm0));
}

inline __m256 avx2_sigmoid_ps(__m256 x) {
    const __m256 one = _mm256_set1_ps(1.0f);
    return _mm256_div_ps(one, _mm256_add_ps(one, avx2_exp_ps(_mm256_sub_ps(_mm256_setzero_ps(), x))));
}

inline __m256 avx2_silu_ps(__m256 x) {
    return _mm256_mul_ps(x, avx2_sigmoid_ps(x));
}

inline __m256 avx2_tanh_ps(__m256 x) {
    const __m256 two = _mm256_set1_ps(2.0f);
    const __m256 one = _mm256_set1_ps(1.0f);
    return _mm256_fmsub_ps(two, avx2_sigmoid_ps(_mm256_mul_ps(two, x)), one);
}

inline float avx2_hsum_ps(__m256 v) {
    __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    return _mm_cvtss_f32(s);
}

inline void avx2_sgemm_bias_skip_act(
    const float* __restrict A,
    const float* __restrict B,
    const float* __restrict bias,
    const float* __restrict skip,
    float* __restrict pre_out,
    float* __restrict act_out,
    uint32_t M,
    uint32_t N,
    uint32_t K,
    bool silu
) {
    uint32_t m = 0;
    for (; m + 4 <= M; m += 4) {
        const float* a0 = A + (m + 0) * K;
        const float* a1 = A + (m + 1) * K;
        const float* a2 = A + (m + 2) * K;
        const float* a3 = A + (m + 3) * K;

        uint32_t n = 0;
        for (; n + 24 <= N; n += 24) {
            __m256 b0 = bias ? _mm256_loadu_ps(bias + n + 0) : _mm256_setzero_ps();
            __m256 b1 = bias ? _mm256_loadu_ps(bias + n + 8) : _mm256_setzero_ps();
            __m256 b2 = bias ? _mm256_loadu_ps(bias + n + 16) : _mm256_setzero_ps();

            __m256 acc00 = b0, acc01 = b1, acc02 = b2;
            __m256 acc10 = b0, acc11 = b1, acc12 = b2;
            __m256 acc20 = b0, acc21 = b1, acc22 = b2;
            __m256 acc30 = b0, acc31 = b1, acc32 = b2;

            const float* bp = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m256 bv0 = _mm256_loadu_ps(bp + 0);
                __m256 bv1 = _mm256_loadu_ps(bp + 8);
                __m256 bv2 = _mm256_loadu_ps(bp + 16);

                __m256 av0 = _mm256_set1_ps(a0[k]);
                acc00 = _mm256_fmadd_ps(av0, bv0, acc00);
                acc01 = _mm256_fmadd_ps(av0, bv1, acc01);
                acc02 = _mm256_fmadd_ps(av0, bv2, acc02);

                __m256 av1 = _mm256_set1_ps(a1[k]);
                acc10 = _mm256_fmadd_ps(av1, bv0, acc10);
                acc11 = _mm256_fmadd_ps(av1, bv1, acc11);
                acc12 = _mm256_fmadd_ps(av1, bv2, acc12);

                __m256 av2 = _mm256_set1_ps(a2[k]);
                acc20 = _mm256_fmadd_ps(av2, bv0, acc20);
                acc21 = _mm256_fmadd_ps(av2, bv1, acc21);
                acc22 = _mm256_fmadd_ps(av2, bv2, acc22);

                __m256 av3 = _mm256_set1_ps(a3[k]);
                acc30 = _mm256_fmadd_ps(av3, bv0, acc30);
                acc31 = _mm256_fmadd_ps(av3, bv1, acc31);
                acc32 = _mm256_fmadd_ps(av3, bv2, acc32);

                bp += N;
            }
            if (pre_out) {
                _mm256_storeu_ps(pre_out + (m + 0) * N + n + 0, acc00);
                _mm256_storeu_ps(pre_out + (m + 0) * N + n + 8, acc01);
                _mm256_storeu_ps(pre_out + (m + 0) * N + n + 16, acc02);
                _mm256_storeu_ps(pre_out + (m + 1) * N + n + 0, acc10);
                _mm256_storeu_ps(pre_out + (m + 1) * N + n + 8, acc11);
                _mm256_storeu_ps(pre_out + (m + 1) * N + n + 16, acc12);
                _mm256_storeu_ps(pre_out + (m + 2) * N + n + 0, acc20);
                _mm256_storeu_ps(pre_out + (m + 2) * N + n + 8, acc21);
                _mm256_storeu_ps(pre_out + (m + 2) * N + n + 16, acc22);
                _mm256_storeu_ps(pre_out + (m + 3) * N + n + 0, acc30);
                _mm256_storeu_ps(pre_out + (m + 3) * N + n + 8, acc31);
                _mm256_storeu_ps(pre_out + (m + 3) * N + n + 16, acc32);
            }
            if (act_out) {
                if (silu) {
                    acc00 = avx2_silu_ps(acc00); acc01 = avx2_silu_ps(acc01); acc02 = avx2_silu_ps(acc02);
                    acc10 = avx2_silu_ps(acc10); acc11 = avx2_silu_ps(acc11); acc12 = avx2_silu_ps(acc12);
                    acc20 = avx2_silu_ps(acc20); acc21 = avx2_silu_ps(acc21); acc22 = avx2_silu_ps(acc22);
                    acc30 = avx2_silu_ps(acc30); acc31 = avx2_silu_ps(acc31); acc32 = avx2_silu_ps(acc32);
                }
                if (skip) {
                    const float* s0 = skip + (m + 0) * N + n;
                    const float* s1 = skip + (m + 1) * N + n;
                    const float* s2 = skip + (m + 2) * N + n;
                    const float* s3 = skip + (m + 3) * N + n;
                    acc00 = _mm256_add_ps(acc00, _mm256_loadu_ps(s0 + 0));
                    acc01 = _mm256_add_ps(acc01, _mm256_loadu_ps(s0 + 8));
                    acc02 = _mm256_add_ps(acc02, _mm256_loadu_ps(s0 + 16));
                    acc10 = _mm256_add_ps(acc10, _mm256_loadu_ps(s1 + 0));
                    acc11 = _mm256_add_ps(acc11, _mm256_loadu_ps(s1 + 8));
                    acc12 = _mm256_add_ps(acc12, _mm256_loadu_ps(s1 + 16));
                    acc20 = _mm256_add_ps(acc20, _mm256_loadu_ps(s2 + 0));
                    acc21 = _mm256_add_ps(acc21, _mm256_loadu_ps(s2 + 8));
                    acc22 = _mm256_add_ps(acc22, _mm256_loadu_ps(s2 + 16));
                    acc30 = _mm256_add_ps(acc30, _mm256_loadu_ps(s3 + 0));
                    acc31 = _mm256_add_ps(acc31, _mm256_loadu_ps(s3 + 8));
                    acc32 = _mm256_add_ps(acc32, _mm256_loadu_ps(s3 + 16));
                }
                _mm256_storeu_ps(act_out + (m + 0) * N + n + 0, acc00);
                _mm256_storeu_ps(act_out + (m + 0) * N + n + 8, acc01);
                _mm256_storeu_ps(act_out + (m + 0) * N + n + 16, acc02);
                _mm256_storeu_ps(act_out + (m + 1) * N + n + 0, acc10);
                _mm256_storeu_ps(act_out + (m + 1) * N + n + 8, acc11);
                _mm256_storeu_ps(act_out + (m + 1) * N + n + 16, acc12);
                _mm256_storeu_ps(act_out + (m + 2) * N + n + 0, acc20);
                _mm256_storeu_ps(act_out + (m + 2) * N + n + 8, acc21);
                _mm256_storeu_ps(act_out + (m + 2) * N + n + 16, acc22);
                _mm256_storeu_ps(act_out + (m + 3) * N + n + 0, acc30);
                _mm256_storeu_ps(act_out + (m + 3) * N + n + 8, acc31);
                _mm256_storeu_ps(act_out + (m + 3) * N + n + 16, acc32);
            }
        }
        for (; n + 8 <= N; n += 8) {
            __m256 b0 = bias ? _mm256_loadu_ps(bias + n) : _mm256_setzero_ps();
            __m256 acc0 = b0, acc1 = b0, acc2 = b0, acc3 = b0;
            const float* bp = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m256 bv = _mm256_loadu_ps(bp);
                acc0 = _mm256_fmadd_ps(_mm256_set1_ps(a0[k]), bv, acc0);
                acc1 = _mm256_fmadd_ps(_mm256_set1_ps(a1[k]), bv, acc1);
                acc2 = _mm256_fmadd_ps(_mm256_set1_ps(a2[k]), bv, acc2);
                acc3 = _mm256_fmadd_ps(_mm256_set1_ps(a3[k]), bv, acc3);
                bp += N;
            }
            if (pre_out) {
                _mm256_storeu_ps(pre_out + (m + 0) * N + n, acc0);
                _mm256_storeu_ps(pre_out + (m + 1) * N + n, acc1);
                _mm256_storeu_ps(pre_out + (m + 2) * N + n, acc2);
                _mm256_storeu_ps(pre_out + (m + 3) * N + n, acc3);
            }
            if (act_out) {
                if (silu) {
                    acc0 = avx2_silu_ps(acc0); acc1 = avx2_silu_ps(acc1);
                    acc2 = avx2_silu_ps(acc2); acc3 = avx2_silu_ps(acc3);
                }
                if (skip) {
                    acc0 = _mm256_add_ps(acc0, _mm256_loadu_ps(skip + (m + 0) * N + n));
                    acc1 = _mm256_add_ps(acc1, _mm256_loadu_ps(skip + (m + 1) * N + n));
                    acc2 = _mm256_add_ps(acc2, _mm256_loadu_ps(skip + (m + 2) * N + n));
                    acc3 = _mm256_add_ps(acc3, _mm256_loadu_ps(skip + (m + 3) * N + n));
                }
                _mm256_storeu_ps(act_out + (m + 0) * N + n, acc0);
                _mm256_storeu_ps(act_out + (m + 1) * N + n, acc1);
                _mm256_storeu_ps(act_out + (m + 2) * N + n, acc2);
                _mm256_storeu_ps(act_out + (m + 3) * N + n, acc3);
            }
        }
        for (; n < N; ++n) {
            float b0 = bias ? bias[n] : 0.0f;
            float s0 = b0, s1 = b0, s2 = b0, s3 = b0;
            for (uint32_t k = 0; k < K; ++k) {
                float bk = B[k * N + n];
                s0 += a0[k] * bk; s1 += a1[k] * bk; s2 += a2[k] * bk; s3 += a3[k] * bk;
            }
            if (pre_out) {
                pre_out[(m + 0) * N + n] = s0; pre_out[(m + 1) * N + n] = s1;
                pre_out[(m + 2) * N + n] = s2; pre_out[(m + 3) * N + n] = s3;
            }
            if (act_out) {
                float v0 = silu ? strafenet::fn_silu(s0) : s0;
                float v1 = silu ? strafenet::fn_silu(s1) : s1;
                float v2 = silu ? strafenet::fn_silu(s2) : s2;
                float v3 = silu ? strafenet::fn_silu(s3) : s3;
                if (skip) {
                    v0 += skip[(m + 0) * N + n]; v1 += skip[(m + 1) * N + n];
                    v2 += skip[(m + 2) * N + n]; v3 += skip[(m + 3) * N + n];
                }
                act_out[(m + 0) * N + n] = v0; act_out[(m + 1) * N + n] = v1;
                act_out[(m + 2) * N + n] = v2; act_out[(m + 3) * N + n] = v3;
            }
        }
    }

    for (; m < M; ++m) {
        const float* a0 = A + m * K;
        uint32_t n = 0;
        for (; n + 64 <= N; n += 64) {
            __m256 acc0 = bias ? _mm256_loadu_ps(bias + n + 0) : _mm256_setzero_ps();
            __m256 acc1 = bias ? _mm256_loadu_ps(bias + n + 8) : _mm256_setzero_ps();
            __m256 acc2 = bias ? _mm256_loadu_ps(bias + n + 16) : _mm256_setzero_ps();
            __m256 acc3 = bias ? _mm256_loadu_ps(bias + n + 24) : _mm256_setzero_ps();
            __m256 acc4 = bias ? _mm256_loadu_ps(bias + n + 32) : _mm256_setzero_ps();
            __m256 acc5 = bias ? _mm256_loadu_ps(bias + n + 40) : _mm256_setzero_ps();
            __m256 acc6 = bias ? _mm256_loadu_ps(bias + n + 48) : _mm256_setzero_ps();
            __m256 acc7 = bias ? _mm256_loadu_ps(bias + n + 56) : _mm256_setzero_ps();
            const float* bp = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m256 av = _mm256_set1_ps(a0[k]);
                acc0 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 0), acc0);
                acc1 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 8), acc1);
                acc2 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 16), acc2);
                acc3 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 24), acc3);
                acc4 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 32), acc4);
                acc5 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 40), acc5);
                acc6 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 48), acc6);
                acc7 = _mm256_fmadd_ps(av, _mm256_loadu_ps(bp + 56), acc7);
                bp += N;
            }
            if (pre_out) {
                float* po = pre_out + m * N + n;
                _mm256_storeu_ps(po + 0, acc0); _mm256_storeu_ps(po + 8, acc1);
                _mm256_storeu_ps(po + 16, acc2); _mm256_storeu_ps(po + 24, acc3);
                _mm256_storeu_ps(po + 32, acc4); _mm256_storeu_ps(po + 40, acc5);
                _mm256_storeu_ps(po + 48, acc6); _mm256_storeu_ps(po + 56, acc7);
            }
            if (act_out) {
                if (silu) {
                    acc0 = avx2_silu_ps(acc0); acc1 = avx2_silu_ps(acc1);
                    acc2 = avx2_silu_ps(acc2); acc3 = avx2_silu_ps(acc3);
                    acc4 = avx2_silu_ps(acc4); acc5 = avx2_silu_ps(acc5);
                    acc6 = avx2_silu_ps(acc6); acc7 = avx2_silu_ps(acc7);
                }
                if (skip) {
                    const float* sp = skip + m * N + n;
                    acc0 = _mm256_add_ps(acc0, _mm256_loadu_ps(sp + 0));
                    acc1 = _mm256_add_ps(acc1, _mm256_loadu_ps(sp + 8));
                    acc2 = _mm256_add_ps(acc2, _mm256_loadu_ps(sp + 16));
                    acc3 = _mm256_add_ps(acc3, _mm256_loadu_ps(sp + 24));
                    acc4 = _mm256_add_ps(acc4, _mm256_loadu_ps(sp + 32));
                    acc5 = _mm256_add_ps(acc5, _mm256_loadu_ps(sp + 40));
                    acc6 = _mm256_add_ps(acc6, _mm256_loadu_ps(sp + 48));
                    acc7 = _mm256_add_ps(acc7, _mm256_loadu_ps(sp + 56));
                }
                float* ao = act_out + m * N + n;
                _mm256_storeu_ps(ao + 0, acc0); _mm256_storeu_ps(ao + 8, acc1);
                _mm256_storeu_ps(ao + 16, acc2); _mm256_storeu_ps(ao + 24, acc3);
                _mm256_storeu_ps(ao + 32, acc4); _mm256_storeu_ps(ao + 40, acc5);
                _mm256_storeu_ps(ao + 48, acc6); _mm256_storeu_ps(ao + 56, acc7);
            }
        }
        for (; n + 8 <= N; n += 8) {
            __m256 acc = bias ? _mm256_loadu_ps(bias + n) : _mm256_setzero_ps();
            const float* bp = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                acc = _mm256_fmadd_ps(_mm256_set1_ps(a0[k]), _mm256_loadu_ps(bp), acc);
                bp += N;
            }
            if (pre_out) _mm256_storeu_ps(pre_out + m * N + n, acc);
            if (act_out) {
                if (silu) acc = avx2_silu_ps(acc);
                if (skip) acc = _mm256_add_ps(acc, _mm256_loadu_ps(skip + m * N + n));
                _mm256_storeu_ps(act_out + m * N + n, acc);
            }
        }
        for (; n < N; ++n) {
            float acc = bias ? bias[n] : 0.0f;
            for (uint32_t k = 0; k < K; ++k) acc += a0[k] * B[k * N + n];
            if (pre_out) pre_out[m * N + n] = acc;
            if (act_out) {
                float v = silu ? strafenet::fn_silu(acc) : acc;
                if (skip) v += skip[m * N + n];
                act_out[m * N + n] = v;
            }
        }
    }
}

inline void avx2_sgemm_tn(const float* __restrict A, const float* __restrict B, float* __restrict C, uint32_t I, uint32_t J, uint32_t K_batch) {
    uint32_t i = 0;
    for (; i + 4 <= I; i += 4) {
        uint32_t j = 0;
        for (; j + 24 <= J; j += 24) {
            __m256 c00 = _mm256_setzero_ps(), c01 = _mm256_setzero_ps(), c02 = _mm256_setzero_ps();
            __m256 c10 = _mm256_setzero_ps(), c11 = _mm256_setzero_ps(), c12 = _mm256_setzero_ps();
            __m256 c20 = _mm256_setzero_ps(), c21 = _mm256_setzero_ps(), c22 = _mm256_setzero_ps();
            __m256 c30 = _mm256_setzero_ps(), c31 = _mm256_setzero_ps(), c32 = _mm256_setzero_ps();
            for (uint32_t k = 0; k < K_batch; ++k) {
                const float* ap = A + k * I + i;
                const float* bp = B + k * J + j;
                __m256 b0 = _mm256_loadu_ps(bp + 0);
                __m256 b1 = _mm256_loadu_ps(bp + 8);
                __m256 b2 = _mm256_loadu_ps(bp + 16);
                __m256 a0 = _mm256_set1_ps(ap[0]);
                c00 = _mm256_fmadd_ps(a0, b0, c00); c01 = _mm256_fmadd_ps(a0, b1, c01); c02 = _mm256_fmadd_ps(a0, b2, c02);
                __m256 a1 = _mm256_set1_ps(ap[1]);
                c10 = _mm256_fmadd_ps(a1, b0, c10); c11 = _mm256_fmadd_ps(a1, b1, c11); c12 = _mm256_fmadd_ps(a1, b2, c12);
                __m256 a2 = _mm256_set1_ps(ap[2]);
                c20 = _mm256_fmadd_ps(a2, b0, c20); c21 = _mm256_fmadd_ps(a2, b1, c21); c22 = _mm256_fmadd_ps(a2, b2, c22);
                __m256 a3 = _mm256_set1_ps(ap[3]);
                c30 = _mm256_fmadd_ps(a3, b0, c30); c31 = _mm256_fmadd_ps(a3, b1, c31); c32 = _mm256_fmadd_ps(a3, b2, c32);
            }
            _mm256_storeu_ps(C + (i + 0) * J + j + 0, c00); _mm256_storeu_ps(C + (i + 0) * J + j + 8, c01); _mm256_storeu_ps(C + (i + 0) * J + j + 16, c02);
            _mm256_storeu_ps(C + (i + 1) * J + j + 0, c10); _mm256_storeu_ps(C + (i + 1) * J + j + 8, c11); _mm256_storeu_ps(C + (i + 1) * J + j + 16, c12);
            _mm256_storeu_ps(C + (i + 2) * J + j + 0, c20); _mm256_storeu_ps(C + (i + 2) * J + j + 8, c21); _mm256_storeu_ps(C + (i + 2) * J + j + 16, c22);
            _mm256_storeu_ps(C + (i + 3) * J + j + 0, c30); _mm256_storeu_ps(C + (i + 3) * J + j + 8, c31); _mm256_storeu_ps(C + (i + 3) * J + j + 16, c32);
        }
        for (; j + 8 <= J; j += 8) {
            __m256 c0 = _mm256_setzero_ps(), c1 = _mm256_setzero_ps(), c2 = _mm256_setzero_ps(), c3 = _mm256_setzero_ps();
            for (uint32_t k = 0; k < K_batch; ++k) {
                const float* ap = A + k * I + i;
                __m256 bv = _mm256_loadu_ps(B + k * J + j);
                c0 = _mm256_fmadd_ps(_mm256_set1_ps(ap[0]), bv, c0);
                c1 = _mm256_fmadd_ps(_mm256_set1_ps(ap[1]), bv, c1);
                c2 = _mm256_fmadd_ps(_mm256_set1_ps(ap[2]), bv, c2);
                c3 = _mm256_fmadd_ps(_mm256_set1_ps(ap[3]), bv, c3);
            }
            _mm256_storeu_ps(C + (i + 0) * J + j, c0);
            _mm256_storeu_ps(C + (i + 1) * J + j, c1);
            _mm256_storeu_ps(C + (i + 2) * J + j, c2);
            _mm256_storeu_ps(C + (i + 3) * J + j, c3);
        }
        for (; j < J; ++j) {
            float c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            for (uint32_t k = 0; k < K_batch; ++k) {
                float bkj = B[k * J + j];
                c0 += A[k * I + i + 0] * bkj;
                c1 += A[k * I + i + 1] * bkj;
                c2 += A[k * I + i + 2] * bkj;
                c3 += A[k * I + i + 3] * bkj;
            }
            C[(i + 0) * J + j] = c0; C[(i + 1) * J + j] = c1; C[(i + 2) * J + j] = c2; C[(i + 3) * J + j] = c3;
        }
    }
    for (; i < I; ++i) {
        for (uint32_t j = 0; j < J; ++j) {
            float c = 0.0f;
            for (uint32_t k = 0; k < K_batch; ++k) c += A[k * I + i] * B[k * J + j];
            C[i * J + j] = c;
        }
    }
}

inline void avx2_sgemm_nt(const float* __restrict A, const float* __restrict B, float* __restrict C, uint32_t M, uint32_t N, uint32_t K) {
    for (uint32_t m = 0; m < M; ++m) {
        const float* a = A + m * K;
        uint32_t n = 0;
        for (; n + 4 <= N; n += 4) {
            const float* b0 = B + (n + 0) * K;
            const float* b1 = B + (n + 1) * K;
            const float* b2 = B + (n + 2) * K;
            const float* b3 = B + (n + 3) * K;
            __m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps(), s2 = _mm256_setzero_ps(), s3 = _mm256_setzero_ps();
            uint32_t k = 0;
            for (; k + 8 <= K; k += 8) {
                __m256 av = _mm256_loadu_ps(a + k);
                s0 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b0 + k), s0);
                s1 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b1 + k), s1);
                s2 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b2 + k), s2);
                s3 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b3 + k), s3);
            }
            float f0 = avx2_hsum_ps(s0), f1 = avx2_hsum_ps(s1), f2 = avx2_hsum_ps(s2), f3 = avx2_hsum_ps(s3);
            for (; k < K; ++k) {
                f0 += a[k] * b0[k]; f1 += a[k] * b1[k]; f2 += a[k] * b2[k]; f3 += a[k] * b3[k];
            }
            C[m * N + n + 0] = f0; C[m * N + n + 1] = f1; C[m * N + n + 2] = f2; C[m * N + n + 3] = f3;
        }
        for (; n < N; ++n) {
            float f = 0.0f;
            for (uint32_t k = 0; k < K; ++k) f += a[k] * B[n * K + k];
            C[m * N + n] = f;
        }
    }
}

inline void avx2_apply_out_activations(const float* raw, float* out, uint32_t batch) {
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t s = 0; s < strafenet::ModelDims::OUT_STEPS; ++s) {
            const float* r = raw + (b * strafenet::ModelDims::OUT_STEPS + s) * 8;
            float* o = out + (b * strafenet::ModelDims::OUT_STEPS + s) * 8;
            __m256 vr = _mm256_loadu_ps(r);
            __m256 vt = avx2_tanh_ps(vr);
            __m256 vs = avx2_sigmoid_ps(vr);
            alignas(32) float tv[8], sv[8];
            _mm256_store_ps(tv, vt);
            _mm256_store_ps(sv, vs);
            o[0] = tv[0]; o[1] = tv[1]; o[2] = tv[2];
            o[3] = sv[3]; o[4] = sv[4];
            o[5] = strafenet::fn_softplus(r[5]) + 1.0f;
            o[6] = strafenet::fn_softplus(r[6]) + 1.0f;
            o[7] = sv[7];
        }
    }
}

struct CpuWorkspace {
    std::vector<float> ctx1;
    std::vector<float> h_ctx;
    std::vector<float> cols0;
    std::vector<float> cols1;
    std::vector<float> c1;
    std::vector<float> c2;
    std::vector<float> c3;
    std::vector<float> h_seq;
    std::vector<float> gru_x_t;
    std::vector<float> gru_gx;
    std::vector<float> gru_gh;
    std::vector<float> gru_h_cur;
    std::vector<float> gru_h_nxt;
    std::vector<float> z0;
    std::vector<float> pre1;
    std::vector<float> a1;
    std::vector<float> pre2;
    std::vector<float> a2;
    std::vector<float> pre3;
    std::vector<float> a3;
    std::vector<float> raw_out;
    std::vector<float> pred_out;

    std::vector<float> d_raw;
    std::vector<float> d_a3;
    std::vector<float> d_pre3;
    std::vector<float> d_a2;
    std::vector<float> d_pre2;
    std::vector<float> d_a1;
    std::vector<float> d_pre1;
    std::vector<float> grads;

    void ensure(uint32_t batch) {
        using D = strafenet::ModelDims;
        auto rs = [](std::vector<float>& v, size_t n) { if (v.size() < n) v.resize(n); };
        rs(ctx1, batch * D::HIDDEN);
        rs(h_ctx, batch * D::HIDDEN);
        rs(cols0, batch * D::SEQ_LEN * 3 * D::D_SEQ);
        rs(cols1, batch * D::SEQ_LEN * 3 * D::HIDDEN);
        rs(c1, batch * D::SEQ_LEN * D::HIDDEN);
        rs(c2, batch * D::SEQ_LEN * D::HIDDEN);
        rs(c3, batch * D::SEQ_LEN * D::HIDDEN);
        rs(h_seq, batch * D::HIDDEN);
        rs(gru_x_t, batch * D::HIDDEN);
        rs(gru_gx, batch * 3 * D::HIDDEN);
        rs(gru_gh, batch * 3 * D::HIDDEN);
        rs(gru_h_cur, batch * D::HIDDEN);
        rs(gru_h_nxt, batch * D::HIDDEN);
        rs(z0, batch * D::TRUNK_H);
        rs(pre1, batch * D::TRUNK_H);
        rs(a1, batch * D::TRUNK_H);
        rs(pre2, batch * D::HIDDEN);
        rs(a2, batch * D::HIDDEN);
        rs(pre3, batch * D::HIDDEN);
        rs(a3, batch * D::HIDDEN);
        rs(raw_out, batch * D::OUT_TOTAL);
        rs(pred_out, batch * D::OUT_TOTAL);
        rs(d_raw, batch * D::OUT_TOTAL);
        rs(d_a3, batch * D::HIDDEN);
        rs(d_pre3, batch * D::HIDDEN);
        rs(d_a2, batch * D::HIDDEN);
        rs(d_pre2, batch * D::HIDDEN);
        rs(d_a1, batch * D::TRUNK_H);
        rs(d_pre1, batch * D::TRUNK_H);
        rs(grads, D::PLASTIC_TOTAL);
    }
};

inline void avx2_forward(
    const strafenet::StrafeNetWeights& w,
    const float* x_seq,
    const float* x_ctx,
    float* out,
    uint32_t batch,
    CpuWorkspace& ws
) {
    using D = strafenet::ModelDims;
    ws.ensure(batch);
    const float* bb = w.backbone.data();
    const float* pt = w.plastic_theta.data();

    avx2_sgemm_bias_skip_act(x_ctx, bb + w.off_ctx_w1, bb + w.off_ctx_b1, nullptr, nullptr, ws.ctx1.data(), batch, D::HIDDEN, D::D_CTX, true);
    avx2_sgemm_bias_skip_act(ws.ctx1.data(), bb + w.off_ctx_w2, bb + w.off_ctx_b2, nullptr, nullptr, ws.h_ctx.data(), batch, D::HIDDEN, D::HIDDEN, true);

    if (w.arch == strafenet::ArchType::TCN_ResMLP) {
        strafenet::ref_im2col_causal_conv1d(x_seq, ws.cols0.data(), batch, D::SEQ_LEN, D::D_SEQ, 1);
        avx2_sgemm_bias_skip_act(ws.cols0.data(), bb + w.off_tcn_w1, bb + w.off_tcn_b1, nullptr, nullptr, ws.c1.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::D_SEQ, true);

        strafenet::ref_im2col_causal_conv1d(ws.c1.data(), ws.cols1.data(), batch, D::SEQ_LEN, D::HIDDEN, 2);
        avx2_sgemm_bias_skip_act(ws.cols1.data(), bb + w.off_tcn_w2, bb + w.off_tcn_b2, ws.c1.data(), nullptr, ws.c2.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::HIDDEN, true);

        strafenet::ref_im2col_causal_conv1d(ws.c2.data(), ws.cols1.data(), batch, D::SEQ_LEN, D::HIDDEN, 4);
        avx2_sgemm_bias_skip_act(ws.cols1.data(), bb + w.off_tcn_w3, bb + w.off_tcn_b3, ws.c2.data(), nullptr, ws.c3.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::HIDDEN, true);

        for (uint32_t b = 0; b < batch; ++b) {
            const float* last = ws.c3.data() + (b * D::SEQ_LEN + (D::SEQ_LEN - 1)) * D::HIDDEN;
            std::memcpy(ws.h_seq.data() + b * D::HIDDEN, last, D::HIDDEN * sizeof(float));
        }
    } else {
        strafenet::ref_im2col_causal_conv1d(x_seq, ws.cols0.data(), batch, D::SEQ_LEN, D::D_SEQ, 1);
        avx2_sgemm_bias_skip_act(ws.cols0.data(), bb + w.off_gru_conv_w, bb + w.off_gru_conv_b, nullptr, nullptr, ws.c1.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::D_SEQ, true);

        constexpr uint32_t H = D::HIDDEN;
        constexpr uint32_t H3 = 3 * H;
        std::fill_n(ws.gru_h_cur.data(), batch * H, 0.0f);
        const __m256 one = _mm256_set1_ps(1.0f);
        for (uint32_t t = 0; t < D::SEQ_LEN; ++t) {
            for (uint32_t b = 0; b < batch; ++b) {
                std::memcpy(ws.gru_x_t.data() + b * H, ws.c1.data() + (b * D::SEQ_LEN + t) * H, H * sizeof(float));
            }
            avx2_sgemm_bias_skip_act(ws.gru_x_t.data(), bb + w.off_gru_w_ih, bb + w.off_gru_b_ih, nullptr, nullptr, ws.gru_gx.data(), batch, H3, H, false);
            avx2_sgemm_bias_skip_act(ws.gru_h_cur.data(), bb + w.off_gru_w_hh, bb + w.off_gru_b_hh, nullptr, nullptr, ws.gru_gh.data(), batch, H3, H, false);

            for (uint32_t b = 0; b < batch; ++b) {
                const float* gxb = ws.gru_gx.data() + b * H3;
                const float* ghb = ws.gru_gh.data() + b * H3;
                const float* hpb = ws.gru_h_cur.data() + b * H;
                float* hob = ws.gru_h_nxt.data() + b * H;
                for (uint32_t i = 0; i < H; i += 8) {
                    __m256 z = avx2_sigmoid_ps(_mm256_add_ps(_mm256_loadu_ps(gxb + i), _mm256_loadu_ps(ghb + i)));
                    __m256 r = avx2_sigmoid_ps(_mm256_add_ps(_mm256_loadu_ps(gxb + H + i), _mm256_loadu_ps(ghb + H + i)));
                    __m256 ng = avx2_tanh_ps(_mm256_fmadd_ps(r, _mm256_loadu_ps(ghb + 2 * H + i), _mm256_loadu_ps(gxb + 2 * H + i)));
                    __m256 hp = _mm256_loadu_ps(hpb + i);
                    _mm256_storeu_ps(hob + i, _mm256_fmadd_ps(_mm256_sub_ps(one, z), ng, _mm256_mul_ps(z, hp)));
                }
            }
            ws.gru_h_cur.swap(ws.gru_h_nxt);
        }
        std::memcpy(ws.h_seq.data(), ws.gru_h_cur.data(), batch * H * sizeof(float));
    }

    for (uint32_t b = 0; b < batch; ++b) {
        std::memcpy(ws.z0.data() + b * D::TRUNK_H, ws.h_seq.data() + b * D::HIDDEN, D::HIDDEN * sizeof(float));
        std::memcpy(ws.z0.data() + b * D::TRUNK_H + D::HIDDEN, ws.h_ctx.data() + b * D::HIDDEN, D::HIDDEN * sizeof(float));
    }

    avx2_sgemm_bias_skip_act(ws.z0.data(), pt + w.off_tr_w1, pt + w.off_tr_b1, nullptr, ws.pre1.data(), ws.a1.data(), batch, D::TRUNK_H, D::TRUNK_H, true);
    avx2_sgemm_bias_skip_act(ws.a1.data(), pt + w.off_tr_w2, pt + w.off_tr_b2, nullptr, ws.pre2.data(), ws.a2.data(), batch, D::HIDDEN, D::TRUNK_H, true);
    avx2_sgemm_bias_skip_act(ws.a2.data(), pt + w.off_tr_w3, pt + w.off_tr_b3, nullptr, ws.pre3.data(), ws.a3.data(), batch, D::HIDDEN, D::HIDDEN, true);
    avx2_sgemm_bias_skip_act(ws.a3.data(), pt + w.off_tr_w4, pt + w.off_tr_b4, nullptr, ws.raw_out.data(), nullptr, batch, D::OUT_TOTAL, D::HIDDEN, false);
    avx2_apply_out_activations(ws.raw_out.data(), out, batch);
}

inline strafenet::TrainStepMetrics avx2_train_step(
    strafenet::StrafeNetWeights& w,
    strafenet::AdamWState& opt,
    const float* x_seq,
    const float* x_ctx,
    const float* target,
    uint32_t batch,
    const strafenet::TrainConfig& cfg,
    CpuWorkspace& ws
) {
    using D = strafenet::ModelDims;
    ws.ensure(batch);
    avx2_forward(w, x_seq, x_ctx, ws.pred_out.data(), batch, ws);

    strafenet::TrainStepMetrics m{};
    uint32_t n_elem = batch * D::OUT_TOTAL;
    float inv_n = 1.0f / static_cast<float>(n_elem);
    double mse_sum = 0.0;
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t s = 0; s < D::OUT_STEPS; ++s) {
            uint32_t base = (b * D::OUT_STEPS + s) * 8;
            for (uint32_t c = 0; c < 8; ++c) {
                uint32_t idx = base + c;
                float p = ws.pred_out[idx];
                float diff = p - target[idx];
                mse_sum += 0.5 * static_cast<double>(diff) * static_cast<double>(diff);
                float d_p = diff * inv_n;
                if (c <= 2) {
                    ws.d_raw[idx] = d_p * (1.0f - p * p);
                } else if (c == 3 || c == 4 || c == 7) {
                    ws.d_raw[idx] = d_p * p * (1.0f - p);
                } else {
                    ws.d_raw[idx] = d_p * strafenet::fn_sigmoid(ws.raw_out[idx]);
                }
            }
        }
    }
    m.mse_loss = static_cast<float>(mse_sum * inv_n);

    const float* pt = w.plastic_theta.data();
    float* gptr = ws.grads.data();

    avx2_sgemm_tn(ws.a3.data(), ws.d_raw.data(), gptr + w.off_tr_w4, D::HIDDEN, D::OUT_TOTAL, batch);
    for (uint32_t j = 0; j < D::OUT_TOTAL; j += 8) {
        __m256 acc = _mm256_setzero_ps();
        for (uint32_t b = 0; b < batch; ++b) acc = _mm256_add_ps(acc, _mm256_loadu_ps(ws.d_raw.data() + b * D::OUT_TOTAL + j));
        _mm256_storeu_ps(gptr + w.off_tr_b4 + j, acc);
    }

    avx2_sgemm_nt(ws.d_raw.data(), pt + w.off_tr_w4, ws.d_a3.data(), batch, D::HIDDEN, D::OUT_TOTAL);
    {
        const __m256 one = _mm256_set1_ps(1.0f);
        uint32_t cnt = batch * D::HIDDEN;
        for (uint32_t i = 0; i < cnt; i += 8) {
            __m256 x = _mm256_loadu_ps(ws.pre3.data() + i);
            __m256 sig = avx2_sigmoid_ps(x);
            __m256 deriv = _mm256_fmadd_ps(_mm256_mul_ps(x, sig), _mm256_sub_ps(one, sig), sig);
            _mm256_storeu_ps(ws.d_pre3.data() + i, _mm256_mul_ps(_mm256_loadu_ps(ws.d_a3.data() + i), deriv));
        }
    }

    avx2_sgemm_tn(ws.a2.data(), ws.d_pre3.data(), gptr + w.off_tr_w3, D::HIDDEN, D::HIDDEN, batch);
    for (uint32_t j = 0; j < D::HIDDEN; j += 8) {
        __m256 acc = _mm256_setzero_ps();
        for (uint32_t b = 0; b < batch; ++b) acc = _mm256_add_ps(acc, _mm256_loadu_ps(ws.d_pre3.data() + b * D::HIDDEN + j));
        _mm256_storeu_ps(gptr + w.off_tr_b3 + j, acc);
    }

    avx2_sgemm_nt(ws.d_pre3.data(), pt + w.off_tr_w3, ws.d_a2.data(), batch, D::HIDDEN, D::HIDDEN);
    {
        const __m256 one = _mm256_set1_ps(1.0f);
        uint32_t cnt = batch * D::HIDDEN;
        for (uint32_t i = 0; i < cnt; i += 8) {
            __m256 x = _mm256_loadu_ps(ws.pre2.data() + i);
            __m256 sig = avx2_sigmoid_ps(x);
            __m256 deriv = _mm256_fmadd_ps(_mm256_mul_ps(x, sig), _mm256_sub_ps(one, sig), sig);
            _mm256_storeu_ps(ws.d_pre2.data() + i, _mm256_mul_ps(_mm256_loadu_ps(ws.d_a2.data() + i), deriv));
        }
    }

    avx2_sgemm_tn(ws.a1.data(), ws.d_pre2.data(), gptr + w.off_tr_w2, D::TRUNK_H, D::HIDDEN, batch);
    for (uint32_t j = 0; j < D::HIDDEN; j += 8) {
        __m256 acc = _mm256_setzero_ps();
        for (uint32_t b = 0; b < batch; ++b) acc = _mm256_add_ps(acc, _mm256_loadu_ps(ws.d_pre2.data() + b * D::HIDDEN + j));
        _mm256_storeu_ps(gptr + w.off_tr_b2 + j, acc);
    }

    avx2_sgemm_nt(ws.d_pre2.data(), pt + w.off_tr_w2, ws.d_a1.data(), batch, D::TRUNK_H, D::HIDDEN);
    {
        const __m256 one = _mm256_set1_ps(1.0f);
        uint32_t cnt = batch * D::TRUNK_H;
        for (uint32_t i = 0; i < cnt; i += 8) {
            __m256 x = _mm256_loadu_ps(ws.pre1.data() + i);
            __m256 sig = avx2_sigmoid_ps(x);
            __m256 deriv = _mm256_fmadd_ps(_mm256_mul_ps(x, sig), _mm256_sub_ps(one, sig), sig);
            _mm256_storeu_ps(ws.d_pre1.data() + i, _mm256_mul_ps(_mm256_loadu_ps(ws.d_a1.data() + i), deriv));
        }
    }

    avx2_sgemm_tn(ws.z0.data(), ws.d_pre1.data(), gptr + w.off_tr_w1, D::TRUNK_H, D::TRUNK_H, batch);
    for (uint32_t j = 0; j < D::TRUNK_H; j += 8) {
        __m256 acc = _mm256_setzero_ps();
        for (uint32_t b = 0; b < batch; ++b) acc = _mm256_add_ps(acc, _mm256_loadu_ps(ws.d_pre1.data() + b * D::TRUNK_H + j));
        _mm256_storeu_ps(gptr + w.off_tr_b1 + j, acc);
    }

    __m256 v_l2 = _mm256_setzero_ps();
    __m256 v_nsq = _mm256_setzero_ps();
    const __m256 v_lam = _mm256_set1_ps(cfg.lambda_anchor);
    const __m256 v_half_lam = _mm256_set1_ps(0.5f * cfg.lambda_anchor);
    const float* th0 = w.anchor_theta0.data();
    float* th = w.plastic_theta.data();
    for (uint32_t i = 0; i < D::PLASTIC_TOTAL; i += 8) {
        __m256 diff = _mm256_sub_ps(_mm256_loadu_ps(th + i), _mm256_loadu_ps(th0 + i));
        __m256 g = _mm256_fmadd_ps(v_lam, diff, _mm256_loadu_ps(gptr + i));
        _mm256_storeu_ps(gptr + i, g);
        v_l2 = _mm256_fmadd_ps(v_half_lam, _mm256_mul_ps(diff, diff), v_l2);
        v_nsq = _mm256_fmadd_ps(g, g, v_nsq);
    }
    float l2_loss = avx2_hsum_ps(v_l2);
    float norm_sq = avx2_hsum_ps(v_nsq);
    m.l2_anchor_loss = l2_loss;
    m.total_loss = m.mse_loss + l2_loss;
    m.grad_norm = std::sqrt(norm_sq);

    if (!std::isfinite(m.grad_norm) || !std::isfinite(m.mse_loss)) {
        opt.skipped_nonfinite++;
        m.applied = false;
        return m;
    }

    float clip_scale = (m.grad_norm > cfg.max_grad_norm) ? (cfg.max_grad_norm / (m.grad_norm + 1e-12f)) : 1.0f;
    opt.step++;
    float inv_bc1 = 1.0f / (1.0f - std::pow(cfg.beta1, static_cast<float>(opt.step)));
    float inv_bc2 = 1.0f / (1.0f - std::pow(cfg.beta2, static_cast<float>(opt.step)));

    const __m256 vclip = _mm256_set1_ps(clip_scale);
    const __m256 vb1 = _mm256_set1_ps(cfg.beta1);
    const __m256 v1mb1 = _mm256_set1_ps(1.0f - cfg.beta1);
    const __m256 vb2 = _mm256_set1_ps(cfg.beta2);
    const __m256 v1mb2 = _mm256_set1_ps(1.0f - cfg.beta2);
    const __m256 vdecay = _mm256_set1_ps(1.0f - cfg.lr * cfg.weight_decay);
    const __m256 vlr = _mm256_set1_ps(cfg.lr);
    const __m256 veps = _mm256_set1_ps(cfg.eps);
    const __m256 vbc1 = _mm256_set1_ps(inv_bc1);
    const __m256 vbc2 = _mm256_set1_ps(inv_bc2);

    float* mp = opt.m.data();
    float* vp = opt.v.data();
    for (uint32_t i = 0; i < D::PLASTIC_TOTAL; i += 8) {
        __m256 g = _mm256_mul_ps(_mm256_loadu_ps(gptr + i), vclip);
        __m256 mi = _mm256_fmadd_ps(vb1, _mm256_loadu_ps(mp + i), _mm256_mul_ps(v1mb1, g));
        __m256 vi = _mm256_fmadd_ps(vb2, _mm256_loadu_ps(vp + i), _mm256_mul_ps(v1mb2, _mm256_mul_ps(g, g)));
        _mm256_storeu_ps(mp + i, mi);
        _mm256_storeu_ps(vp + i, vi);
        __m256 m_hat = _mm256_mul_ps(mi, vbc1);
        __m256 v_hat = _mm256_mul_ps(vi, vbc2);
        __m256 step = _mm256_mul_ps(vlr, _mm256_div_ps(m_hat, _mm256_add_ps(_mm256_sqrt_ps(v_hat), veps)));
        __m256 pi = _mm256_fmsub_ps(_mm256_loadu_ps(th + i), vdecay, step);
        _mm256_storeu_ps(th + i, pi);
    }
    m.applied = true;
    return m;
}

inline void sse2_sgemm_bias_skip_act(
    const float* __restrict A,
    const float* __restrict B,
    const float* __restrict bias,
    const float* __restrict skip,
    float* __restrict pre_out,
    float* __restrict act_out,
    uint32_t M,
    uint32_t N,
    uint32_t K,
    bool silu
) {
    for (uint32_t m = 0; m < M; ++m) {
        const float* a = A + m * K;
        uint32_t n = 0;
        for (; n + 4 <= N; n += 4) {
            __m128 acc = bias ? _mm_loadu_ps(bias + n) : _mm_setzero_ps();
            const float* bp = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(a[k]), _mm_loadu_ps(bp)));
                bp += N;
            }
            if (pre_out) _mm_storeu_ps(pre_out + m * N + n, acc);
            if (act_out) {
                if (silu) {
                    alignas(16) float tmp[4];
                    _mm_store_ps(tmp, acc);
                    for (int i = 0; i < 4; ++i) tmp[i] = strafenet::fn_silu(tmp[i]);
                    acc = _mm_load_ps(tmp);
                }
                if (skip) acc = _mm_add_ps(acc, _mm_loadu_ps(skip + m * N + n));
                _mm_storeu_ps(act_out + m * N + n, acc);
            }
        }
        for (; n < N; ++n) {
            float acc = bias ? bias[n] : 0.0f;
            for (uint32_t k = 0; k < K; ++k) acc += a[k] * B[k * N + n];
            if (pre_out) pre_out[m * N + n] = acc;
            if (act_out) {
                float v = silu ? strafenet::fn_silu(acc) : acc;
                if (skip) v += skip[m * N + n];
                act_out[m * N + n] = v;
            }
        }
    }
}

inline void sse2_forward(
    const strafenet::StrafeNetWeights& w,
    const float* x_seq,
    const float* x_ctx,
    float* out,
    uint32_t batch,
    CpuWorkspace& ws
) {
    using D = strafenet::ModelDims;
    ws.ensure(batch);
    const float* bb = w.backbone.data();
    const float* pt = w.plastic_theta.data();

    sse2_sgemm_bias_skip_act(x_ctx, bb + w.off_ctx_w1, bb + w.off_ctx_b1, nullptr, nullptr, ws.ctx1.data(), batch, D::HIDDEN, D::D_CTX, true);
    sse2_sgemm_bias_skip_act(ws.ctx1.data(), bb + w.off_ctx_w2, bb + w.off_ctx_b2, nullptr, nullptr, ws.h_ctx.data(), batch, D::HIDDEN, D::HIDDEN, true);

    if (w.arch == strafenet::ArchType::TCN_ResMLP) {
        strafenet::ref_im2col_causal_conv1d(x_seq, ws.cols0.data(), batch, D::SEQ_LEN, D::D_SEQ, 1);
        sse2_sgemm_bias_skip_act(ws.cols0.data(), bb + w.off_tcn_w1, bb + w.off_tcn_b1, nullptr, nullptr, ws.c1.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::D_SEQ, true);
        strafenet::ref_im2col_causal_conv1d(ws.c1.data(), ws.cols1.data(), batch, D::SEQ_LEN, D::HIDDEN, 2);
        sse2_sgemm_bias_skip_act(ws.cols1.data(), bb + w.off_tcn_w2, bb + w.off_tcn_b2, ws.c1.data(), nullptr, ws.c2.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::HIDDEN, true);
        strafenet::ref_im2col_causal_conv1d(ws.c2.data(), ws.cols1.data(), batch, D::SEQ_LEN, D::HIDDEN, 4);
        sse2_sgemm_bias_skip_act(ws.cols1.data(), bb + w.off_tcn_w3, bb + w.off_tcn_b3, ws.c2.data(), nullptr, ws.c3.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::HIDDEN, true);
        for (uint32_t b = 0; b < batch; ++b) {
            std::memcpy(ws.h_seq.data() + b * D::HIDDEN, ws.c3.data() + (b * D::SEQ_LEN + (D::SEQ_LEN - 1)) * D::HIDDEN, D::HIDDEN * sizeof(float));
        }
    } else {
        strafenet::ref_im2col_causal_conv1d(x_seq, ws.cols0.data(), batch, D::SEQ_LEN, D::D_SEQ, 1);
        sse2_sgemm_bias_skip_act(ws.cols0.data(), bb + w.off_gru_conv_w, bb + w.off_gru_conv_b, nullptr, nullptr, ws.c1.data(), batch * D::SEQ_LEN, D::HIDDEN, 3 * D::D_SEQ, true);
        constexpr uint32_t H = D::HIDDEN;
        constexpr uint32_t H3 = 3 * H;
        std::fill_n(ws.gru_h_cur.data(), batch * H, 0.0f);
        for (uint32_t t = 0; t < D::SEQ_LEN; ++t) {
            for (uint32_t b = 0; b < batch; ++b) {
                std::memcpy(ws.gru_x_t.data() + b * H, ws.c1.data() + (b * D::SEQ_LEN + t) * H, H * sizeof(float));
            }
            sse2_sgemm_bias_skip_act(ws.gru_x_t.data(), bb + w.off_gru_w_ih, bb + w.off_gru_b_ih, nullptr, nullptr, ws.gru_gx.data(), batch, H3, H, false);
            sse2_sgemm_bias_skip_act(ws.gru_h_cur.data(), bb + w.off_gru_w_hh, bb + w.off_gru_b_hh, nullptr, nullptr, ws.gru_gh.data(), batch, H3, H, false);
            for (uint32_t b = 0; b < batch; ++b) {
                for (uint32_t i = 0; i < H; ++i) {
                    float z = strafenet::fn_sigmoid(ws.gru_gx[b * H3 + i] + ws.gru_gh[b * H3 + i]);
                    float r = strafenet::fn_sigmoid(ws.gru_gx[b * H3 + H + i] + ws.gru_gh[b * H3 + H + i]);
                    float n_g = std::tanh(ws.gru_gx[b * H3 + 2 * H + i] + r * ws.gru_gh[b * H3 + 2 * H + i]);
                    ws.gru_h_nxt[b * H + i] = (1.0f - z) * n_g + z * ws.gru_h_cur[b * H + i];
                }
            }
            ws.gru_h_cur.swap(ws.gru_h_nxt);
        }
        std::memcpy(ws.h_seq.data(), ws.gru_h_cur.data(), batch * H * sizeof(float));
    }

    for (uint32_t b = 0; b < batch; ++b) {
        std::memcpy(ws.z0.data() + b * D::TRUNK_H, ws.h_seq.data() + b * D::HIDDEN, D::HIDDEN * sizeof(float));
        std::memcpy(ws.z0.data() + b * D::TRUNK_H + D::HIDDEN, ws.h_ctx.data() + b * D::HIDDEN, D::HIDDEN * sizeof(float));
    }
    sse2_sgemm_bias_skip_act(ws.z0.data(), pt + w.off_tr_w1, pt + w.off_tr_b1, nullptr, ws.pre1.data(), ws.a1.data(), batch, D::TRUNK_H, D::TRUNK_H, true);
    sse2_sgemm_bias_skip_act(ws.a1.data(), pt + w.off_tr_w2, pt + w.off_tr_b2, nullptr, ws.pre2.data(), ws.a2.data(), batch, D::HIDDEN, D::TRUNK_H, true);
    sse2_sgemm_bias_skip_act(ws.a2.data(), pt + w.off_tr_w3, pt + w.off_tr_b3, nullptr, ws.pre3.data(), ws.a3.data(), batch, D::HIDDEN, D::HIDDEN, true);
    sse2_sgemm_bias_skip_act(ws.a3.data(), pt + w.off_tr_w4, pt + w.off_tr_b4, nullptr, ws.raw_out.data(), nullptr, batch, D::OUT_TOTAL, D::HIDDEN, false);
    strafenet::ref_apply_out_activations(ws.raw_out.data(), out, batch);
}

}


namespace strafenet_cuda {

using CUresult = int;
using CUdevice = int;
using CUcontext = void*;
using CUmodule = void*;
using CUfunction = void*;
using CUstream = void*;
using CUevent = void*;
using CUdeviceptr = uint64_t;

static constexpr CUresult CUDA_SUCCESS = 0;
static constexpr int CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16;
static constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75;
static constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76;

static const char* kStrafeNetPtxParts[] = { R"ptx(
.version 6.0
.target sm_50
.address_size 64

.visible .entry sgemm_bias_skip_act(
    .param .u64 p_A,
    .param .u64 p_B,
    .param .u64 p_bias,
    .param .u64 p_skip,
    .param .u64 p_pre,
    .param .u64 p_act,
    .param .u32 p_M,
    .param .u32 p_N,
    .param .u32 p_K,
    .param .u32 p_silu
)
{
    .reg .pred %p<6>;
    .reg .u32 %r<22>;
    .reg .u64 %rd<18>;
    .reg .f32 %f<12>;

    mov.u32 %r10, %ctaid.x;
    mov.u32 %r11, %ntid.x;
    mov.u32 %r12, %tid.x;
    mad.lo.u32 %r1, %r10, %r11, %r12;
    mov.u32 %r13, %ctaid.y;
    mov.u32 %r14, %ntid.y;
    mov.u32 %r15, %tid.y;
    mad.lo.u32 %r2, %r13, %r14, %r15;

    ld.param.u32 %r3, [p_M];
    ld.param.u32 %r4, [p_N];
    ld.param.u32 %r5, [p_K];

    setp.ge.u32 %p1, %r2, %r3;
    setp.ge.u32 %p2, %r1, %r4;
    or.pred %p3, %p1, %p2;
    @%p3 bra DONE;

    ld.param.u64 %rd1, [p_A];
    ld.param.u64 %rd2, [p_B];
    ld.param.u64 %rd3, [p_bias];
    ld.param.u64 %rd4, [p_skip];
    ld.param.u64 %rd5, [p_pre];
    ld.param.u64 %rd6, [p_act];

    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;

    mov.f32 %f1, 0f00000000;
    setp.eq.u64 %p4, %rd3, 0;
    @%p4 bra SKIP_BIAS;
    cvta.to.global.u64 %rd3, %rd3;
    mul.wide.u32 %rd7, %r1, 4;
    add.u64 %rd8, %rd3, %rd7;
    ld.global.f32 %f1, [%rd8];
SKIP_BIAS:

    mul.lo.u32 %r6, %r2, %r5;
    mul.wide.u32 %rd7, %r6, 4;
    add.u64 %rd9, %rd1, %rd7;

    mul.wide.u32 %rd10, %r1, 4;
    add.u64 %rd11, %rd2, %rd10;
    mul.wide.u32 %rd12, %r4, 4;

    mov.u32 %r7, 0;
    setp.eq.u32 %p4, %r5, 0;
    @%p4 bra K_DONE;
K_LOOP:
    ld.global.f32 %f2, [%rd9];
    ld.global.f32 %f3, [%rd11];
    fma.rn.f32 %f1, %f2, %f3, %f1;
    add.u64 %rd9, %rd9, 4;
    add.u64 %rd11, %rd11, %rd12;
    add.u32 %r7, %r7, 1;
    setp.lt.u32 %p4, %r7, %r5;
    @%p4 bra K_LOOP;
K_DONE:

    mad.lo.u32 %r8, %r2, %r4, %r1;
    mul.wide.u32 %rd13, %r8, 4;

    setp.eq.u64 %p4, %rd5, 0;
    @%p4 bra SKIP_PRE;
    cvta.to.global.u64 %rd5, %rd5;
    add.u64 %rd15, %rd5, %rd13;
    st.global.f32 [%rd15], %f1;
SKIP_PRE:

    setp.eq.u64 %p4, %rd6, 0;
    @%p4 bra DONE;
    ld.param.u32 %r9, [p_silu];
    setp.eq.u32 %p5, %r9, 0;
    @%p5 bra SKIP_SILU;

    neg.f32 %f5, %f1;
    mul.f32 %f6, %f5, 0f3FB8AA3B;
    ex2.approx.f32 %f7, %f6;
    add.f32 %f8, %f7, 0f3F800000;
    div.rn.f32 %f9, 0f3F800000, %f8;
    mul.f32 %f1, %f1, %f9;

SKIP_SILU:
    setp.eq.u64 %p4, %rd4, 0;
    @%p4 bra STORE_ACT;
    cvta.to.global.u64 %rd4, %rd4;
    add.u64 %rd14, %rd4, %rd13;
    ld.global.f32 %f4, [%rd14];
    add.f32 %f1, %f1, %f4;

STORE_ACT:
    cvta.to.global.u64 %rd6, %rd6;
    add.u64 %rd16, %rd6, %rd13;
    st.global.f32 [%rd16], %f1;
DONE:
    ret;
}

.visible .entry sgemm_tn(
    .param .u64 p_A,
    .param .u64 p_B,
    .param .u64 p_C,
    .param .u32 p_I,
    .param .u32 p_J,
    .param .u32 p_K
)
{
    .reg .pred %p<5>;
    .reg .u32 %r<16>;
    .reg .u64 %rd<14>;
    .reg .f32 %f<4>;

    mov.u32 %r10, %ctaid.x;
    mov.u32 %r11, %ntid.x;
    mov.u32 %r12, %tid.x;
    mad.lo.u32 %r1, %r10, %r11, %r12;
    mov.u32 %r13, %ctaid.y;
    mov.u32 %r14, %ntid.y;
    mov.u32 %r15, %tid.y;
    mad.lo.u32 %r2, %r13, %r14, %r15;

    ld.param.u32 %r3, [p_I];
    ld.param.u32 %r4, [p_J];
    ld.param.u32 %r5, [p_K];

    setp.ge.u32 %p1, %r2, %r3;
    setp.ge.u32 %p2, %r1, %r4;
    or.pred %p3, %p1, %p2;
    @%p3 bra DONE;

    ld.param.u64 %rd1, [p_A];
    ld.param.u64 %rd2, [p_B];
    ld.param.u64 %rd3, [p_C];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;

    mul.wide.u32 %rd4, %r2, 4;
    add.u64 %rd5, %rd1, %rd4;
    mul.wide.u32 %rd6, %r3, 4;

    mul.wide.u32 %rd7, %r1, 4;
    add.u64 %rd8, %rd2, %rd7;
    mul.wide.u32 %rd9, %r4, 4;

    mov.f32 %f1, 0f00000000;
    mov.u32 %r6, 0;
    setp.eq.u32 %p4, %r5, 0;
    @%p4 bra K_DONE;
K_LOOP:
    ld.global.f32 %f2, [%rd5];
    ld.global.f32 %f3, [%rd8];
    fma.rn.f32 %f1, %f2, %f3, %f1;
    add.u64 %rd5, %rd5, %rd6;
    add.u64 %rd8, %rd8, %rd9;
    add.u32 %r6, %r6, 1;
    setp.lt.u32 %p4, %r6, %r5;
    @%p4 bra K_LOOP;
K_DONE:
    mad.lo.u32 %r7, %r2, %r4, %r1;
    mul.wide.u32 %rd10, %r7, 4;
    add.u64 %rd11, %rd3, %rd10;
    st.global.f32 [%rd11], %f1;
DONE:
    ret;
}

.visible .entry sgemm_nt(
    .param .u64 p_A,
    .param .u64 p_B,
    .param .u64 p_C,
    .param .u32 p_M,
    .param .u32 p_N,
    .param .u32 p_K
)
{
    .reg .pred %p<5>;
    .reg .u32 %r<16>;
    .reg .u64 %rd<12>;
    .reg .f32 %f<4>;

    mov.u32 %r10, %ctaid.x;
    mov.u32 %r11, %ntid.x;
    mov.u32 %r12, %tid.x;
    mad.lo.u32 %r1, %r10, %r11, %r12;
    mov.u32 %r13, %ctaid.y;
    mov.u32 %r14, %ntid.y;
    mov.u32 %r15, %tid.y;
    mad.lo.u32 %r2, %r13, %r14, %r15;

    ld.param.u32 %r3, [p_M];
    ld.param.u32 %r4, [p_N];
    ld.param.u32 %r5, [p_K];

    setp.ge.u32 %p1, %r2, %r3;
    setp.ge.u32 %p2, %r1, %r4;
    or.pred %p3, %p1, %p2;
    @%p3 bra DONE;

    ld.param.u64 %rd1, [p_A];
    ld.param.u64 %rd2, [p_B];
    ld.param.u64 %rd3, [p_C];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;

    mul.lo.u32 %r6, %r2, %r5;
    mul.wide.u32 %rd4, %r6, 4;
    add.u64 %rd5, %rd1, %rd4;

    mul.lo.u32 %r7, %r1, %r5;
    mul.wide.u32 %rd6, %r7, 4;
    add.u64 %rd7, %rd2, %rd6;

    mov.f32 %f1, 0f00000000;
    mov.u32 %r8, 0;
    setp.eq.u32 %p4, %r5, 0;
    @%p4 bra K_DONE;
K_LOOP:
    ld.global.f32 %f2, [%rd5];
    ld.global.f32 %f3, [%rd7];
    fma.rn.f32 %f1, %f2, %f3, %f1;
    add.u64 %rd5, %rd5, 4;
    add.u64 %rd7, %rd7, 4;
    add.u32 %r8, %r8, 1;
    setp.lt.u32 %p4, %r8, %r5;
    @%p4 bra K_LOOP;
K_DONE:
    mad.lo.u32 %r9, %r2, %r4, %r1;
    mul.wide.u32 %rd8, %r9, 4;
    add.u64 %rd9, %rd3, %rd8;
    st.global.f32 [%rd9], %f1;
DONE:
    ret;
}

.visible .entry im2col_causal_conv1d(
    .param .u64 p_in,
    .param .u64 p_cols,
    .param .u32 p_batch,
    .param .u32 p_seq,
    .param .u32 p_cin,
    .param .u32 p_dil
)
{
    .reg .pred %p<3>;
    .reg .u32 %r<19>;
    .reg .s32 %rs<4>;
    .reg .u64 %rd<7>;
    .reg .f32 %f1;

    mov.u32 %r16, %ctaid.x;
    mov.u32 %r17, %ntid.x;
    mov.u32 %r18, %tid.x;
    mad.lo.u32 %r1, %r16, %r17, %r18;
    ld.param.u32 %r2, [p_batch];
    ld.param.u32 %r3, [p_seq];
    ld.param.u32 %r4, [p_cin];
    ld.param.u32 %r5, [p_dil];

    mul.lo.u32 %r6, %r4, 3;
    mul.lo.u32 %r7, %r2, %r3;
    mul.lo.u32 %r8, %r7, %r6;
    setp.ge.u32 %p1, %r1, %r8;
    @%p1 bra DONE;

    div.u32 %r9, %r1, %r6;
    rem.u32 %r10, %r1, %r6;
    div.u32 %r11, %r9, %r3;
    rem.u32 %r12, %r9, %r3;
    div.u32 %r13, %r10, %r4;
    rem.u32 %r14, %r10, %r4;

    mov.u32 %r15, 2;
    sub.u32 %r15, %r15, %r13;
    mul.lo.u32 %r15, %r15, %r5;
    sub.s32 %rs1, %r12, %r15;

    mov.f32 %f1, 0f00000000;
    setp.lt.s32 %p2, %rs1, 0;
    @%p2 bra STORE;

    ld.param.u64 %rd1, [p_in];
    cvta.to.global.u64 %rd1, %rd1;
    mad.lo.u32 %r7, %r11, %r3, %rs1;
    mad.lo.u32 %r7, %r7, %r4, %r14;
    mul.wide.u32 %rd3, %r7, 4;
    add.u64 %rd4, %rd1, %rd3;
    ld.global.f32 %f1, [%rd4];

STORE:
    ld.param.u64 %rd2, [p_cols];
    cvta.to.global.u64 %rd2, %rd2;
    mul.wide.u32 %rd5, %r1, 4;
    add.u64 %rd6, %rd2, %rd5;
    st.global.f32 [%rd6], %f1;
DONE:
    ret;
}
)ptx", R"ptx(
.visible .entry slice_timestep(
    .param .u64 p_in,
    .param .u64 p_out,
    .param .u32 p_batch,
    .param .u32 p_seq,
    .param .u32 p_H,
    .param .u32 p_t
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<13>;
    .reg .u64 %rd<7>;
    .reg .f32 %f1;

    mov.u32 %r10, %ctaid.x;
    mov.u32 %r11, %ntid.x;
    mov.u32 %r12, %tid.x;
    mad.lo.u32 %r1, %r10, %r11, %r12;
    ld.param.u32 %r2, [p_batch];
    ld.param.u32 %r3, [p_H];
    mul.lo.u32 %r4, %r2, %r3;
    setp.ge.u32 %p1, %r1, %r4;
    @%p1 bra DONE;

    div.u32 %r5, %r1, %r3;
    rem.u32 %r6, %r1, %r3;
    ld.param.u32 %r7, [p_seq];
    ld.param.u32 %r8, [p_t];
    mad.lo.u32 %r9, %r5, %r7, %r8;
    mad.lo.u32 %r9, %r9, %r3, %r6;

    ld.param.u64 %rd1, [p_in];
    ld.param.u64 %rd2, [p_out];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    mul.wide.u32 %rd3, %r9, 4;
    add.u64 %rd4, %rd1, %rd3;
    ld.global.f32 %f1, [%rd4];
    mul.wide.u32 %rd5, %r1, 4;
    add.u64 %rd6, %rd2, %rd5;
    st.global.f32 [%rd6], %f1;
DONE:
    ret;
}

.visible .entry concat_2(
    .param .u64 p_A,
    .param .u64 p_B,
    .param .u64 p_out,
    .param .u32 p_batch,
    .param .u32 p_dim
)
{
    .reg .pred %p<3>;
    .reg .u32 %r<12>;
    .reg .u64 %rd<9>;
    .reg .f32 %f1;

    mov.u32 %r9, %ctaid.x;
    mov.u32 %r10, %ntid.x;
    mov.u32 %r11, %tid.x;
    mad.lo.u32 %r1, %r9, %r10, %r11;
    ld.param.u32 %r2, [p_batch];
    ld.param.u32 %r3, [p_dim];
    shl.b32 %r4, %r3, 1;
    mul.lo.u32 %r5, %r2, %r4;
    setp.ge.u32 %p1, %r1, %r5;
    @%p1 bra DONE;

    div.u32 %r6, %r1, %r4;
    rem.u32 %r7, %r1, %r4;

    ld.param.u64 %rd1, [p_A];
    ld.param.u64 %rd2, [p_B];
    ld.param.u64 %rd3, [p_out];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;

    setp.ge.u32 %p2, %r7, %r3;
    @%p2 bra FROM_B;
    mad.lo.u32 %r8, %r6, %r3, %r7;
    mul.wide.u32 %rd4, %r8, 4;
    add.u64 %rd5, %rd1, %rd4;
    ld.global.f32 %f1, [%rd5];
    bra STORE;
FROM_B:
    sub.u32 %r7, %r7, %r3;
    mad.lo.u32 %r8, %r6, %r3, %r7;
    mul.wide.u32 %rd4, %r8, 4;
    add.u64 %rd5, %rd2, %rd4;
    ld.global.f32 %f1, [%rd5];
STORE:
    mul.wide.u32 %rd6, %r1, 4;
    add.u64 %rd7, %rd3, %rd6;
    st.global.f32 [%rd7], %f1;
DONE:
    ret;
}

.visible .entry gru_gates(
    .param .u64 p_gx,
    .param .u64 p_gh,
    .param .u64 p_hp,
    .param .u64 p_ho,
    .param .u32 p_batch,
    .param .u32 p_H
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<16>;
    .reg .u64 %rd<16>;
    .reg .f32 %f<26>;

    mov.u32 %r1, %ctaid.x;
    mov.u32 %r2, %ntid.x;
    mov.u32 %r3, %tid.x;
    mad.lo.u32 %r4, %r1, %r2, %r3;
    ld.param.u32 %r5, [p_batch];
    ld.param.u32 %r6, [p_H];
    mul.lo.u32 %r7, %r5, %r6;
    setp.ge.u32 %p1, %r4, %r7;
    @%p1 bra DONE;

    div.u32 %r8, %r4, %r6;
    rem.u32 %r9, %r4, %r6;
    mul.lo.u32 %r10, %r6, 3;
    mad.lo.u32 %r11, %r8, %r10, %r9;
    add.u32 %r12, %r11, %r6;
    add.u32 %r13, %r12, %r6;

    ld.param.u64 %rd1, [p_gx];
    ld.param.u64 %rd2, [p_gh];
    ld.param.u64 %rd3, [p_hp];
    ld.param.u64 %rd4, [p_ho];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;
    cvta.to.global.u64 %rd4, %rd4;

    mul.wide.u32 %rd5, %r11, 4;
    mul.wide.u32 %rd6, %r12, 4;
    mul.wide.u32 %rd7, %r13, 4;

    add.u64 %rd8, %rd1, %rd5; ld.global.f32 %f1, [%rd8];
    add.u64 %rd8, %rd2, %rd5; ld.global.f32 %f2, [%rd8];
    add.u64 %rd8, %rd1, %rd6; ld.global.f32 %f3, [%rd8];
    add.u64 %rd8, %rd2, %rd6; ld.global.f32 %f4, [%rd8];
    add.u64 %rd8, %rd1, %rd7; ld.global.f32 %f5, [%rd8];
    add.u64 %rd8, %rd2, %rd7; ld.global.f32 %f6, [%rd8];

    add.f32 %f7, %f1, %f2;
    neg.f32 %f8, %f7;
    mul.f32 %f9, %f8, 0f3FB8AA3B;
    ex2.approx.f32 %f10, %f9;
    add.f32 %f11, %f10, 0f3F800000;
    div.rn.f32 %f12, 0f3F800000, %f11;

    add.f32 %f13, %f3, %f4;
    neg.f32 %f14, %f13;
    mul.f32 %f15, %f14, 0f3FB8AA3B;
    ex2.approx.f32 %f16, %f15;
    add.f32 %f17, %f16, 0f3F800000;
    div.rn.f32 %f18, 0f3F800000, %f17;

    fma.rn.f32 %f19, %f18, %f6, %f5;
    mul.f32 %f20, %f19, 0f40000000;
    neg.f32 %f21, %f20;
    mul.f32 %f22, %f21, 0f3FB8AA3B;
    ex2.approx.f32 %f23, %f22;
    add.f32 %f24, %f23, 0f3F800000;
    div.rn.f32 %f25, 0f3F800000, %f24;
    fma.rn.f32 %f25, %f25, 0f40000000, 0fBF800000;

    mul.wide.u32 %rd9, %r4, 4;
    add.u64 %rd10, %rd3, %rd9;
    add.u64 %rd11, %rd4, %rd9;
    ld.global.f32 %f1, [%rd10];

    sub.f32 %f2, 0f3F800000, %f12;
    mul.f32 %f3, %f2, %f25;
    fma.rn.f32 %f4, %f12, %f1, %f3;
    st.global.f32 [%rd11], %f4;
DONE:
    ret;
}

.visible .entry strafenet_out_act(
    .param .u64 p_raw,
    .param .u64 p_out,
    .param .u32 p_N
)
{
    .reg .pred %p<5>;
    .reg .u32 %r<7>;
    .reg .u64 %rd<6>;
    .reg .f32 %f<10>;

    mov.u32 %r4, %ctaid.x;
    mov.u32 %r5, %ntid.x;
    mov.u32 %r6, %tid.x;
    mad.lo.u32 %r1, %r4, %r5, %r6;
    ld.param.u32 %r2, [p_N];
    setp.ge.u32 %p1, %r1, %r2;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_raw];
    ld.param.u64 %rd2, [p_out];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    mul.wide.u32 %rd3, %r1, 4;
    add.u64 %rd4, %rd1, %rd3;
    add.u64 %rd5, %rd2, %rd3;
    ld.global.f32 %f1, [%rd4];

    and.b32 %r3, %r1, 7;
    setp.le.u32 %p2, %r3, 2;
    @%p2 bra DO_TANH;
    setp.eq.u32 %p3, %r3, 5;
    setp.eq.u32 %p4, %r3, 6;
    or.pred %p2, %p3, %p4;
    @%p2 bra DO_SOFTPLUS;

    neg.f32 %f2, %f1;
    mul.f32 %f3, %f2, 0f3FB8AA3B;
    ex2.approx.f32 %f4, %f3;
    add.f32 %f5, %f4, 0f3F800000;
    div.rn.f32 %f6, 0f3F800000, %f5;
    bra STORE;

DO_TANH:
    mul.f32 %f2, %f1, 0f40000000;
    neg.f32 %f3, %f2;
    mul.f32 %f4, %f3, 0f3FB8AA3B;
    ex2.approx.f32 %f5, %f4;
    add.f32 %f6, %f5, 0f3F800000;
    div.rn.f32 %f7, 0f3F800000, %f6;
    fma.rn.f32 %f6, %f7, 0f40000000, 0fBF800000;
    bra STORE;

DO_SOFTPLUS:
    max.f32 %f2, %f1, 0f00000000;
    abs.f32 %f3, %f1;
    neg.f32 %f3, %f3;
    mul.f32 %f4, %f3, 0f3FB8AA3B;
    ex2.approx.f32 %f5, %f4;
    add.f32 %f6, %f5, 0f3F800000;
    lg2.approx.f32 %f7, %f6;
    fma.rn.f32 %f8, %f7, 0f3F317218, %f2;
    add.f32 %f6, %f8, 0f3F800000;

STORE:
    st.global.f32 [%rd5], %f6;
DONE:
    ret;
}

.visible .entry strafenet_mse_out_grad(
    .param .u64 p_pred,
    .param .u64 p_target,
    .param .u64 p_raw,
    .param .u64 p_draw,
    .param .u64 p_mse_sum,
    .param .u32 p_N
)
{
    .reg .pred %p<5>;
    .reg .u32 %r<7>;
    .reg .u64 %rd<11>;
    .reg .f32 %f<12>;

    mov.u32 %r4, %ctaid.x;
    mov.u32 %r5, %ntid.x;
    mov.u32 %r6, %tid.x;
    mad.lo.u32 %r1, %r4, %r5, %r6;
    ld.param.u32 %r2, [p_N];
    setp.ge.u32 %p1, %r1, %r2;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_pred];
    ld.param.u64 %rd2, [p_target];
    ld.param.u64 %rd3, [p_raw];
    ld.param.u64 %rd4, [p_draw];
    ld.param.u64 %rd5, [p_mse_sum];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;
    cvta.to.global.u64 %rd4, %rd4;
    cvta.to.global.u64 %rd5, %rd5;

    mul.wide.u32 %rd6, %r1, 4;
    add.u64 %rd7, %rd1, %rd6;
    add.u64 %rd8, %rd2, %rd6;
    add.u64 %rd9, %rd3, %rd6;
    add.u64 %rd10, %rd4, %rd6;

    ld.global.f32 %f1, [%rd7];
    ld.global.f32 %f2, [%rd8];
    sub.f32 %f3, %f1, %f2;

    mul.f32 %f4, %f3, %f3;
    mul.f32 %f4, %f4, 0f3F000000;
    red.global.add.f32 [%rd5], %f4;

    cvt.rn.f32.u32 %f5, %r2;
    div.rn.f32 %f6, %f3, %f5;

    and.b32 %r3, %r1, 7;
    setp.le.u32 %p2, %r3, 2;
    @%p2 bra G_TANH;
    setp.eq.u32 %p3, %r3, 5;
    setp.eq.u32 %p4, %r3, 6;
    or.pred %p2, %p3, %p4;
    @%p2 bra G_SOFTPLUS;

    sub.f32 %f7, 0f3F800000, %f1;
    mul.f32 %f8, %f1, %f7;
    mul.f32 %f9, %f6, %f8;
    bra STORE;

G_TANH:
    mul.f32 %f7, %f1, %f1;
    sub.f32 %f8, 0f3F800000, %f7;
    mul.f32 %f9, %f6, %f8;
    bra STORE;

G_SOFTPLUS:
    ld.global.f32 %f7, [%rd9];
    neg.f32 %f8, %f7;
    mul.f32 %f8, %f8, 0f3FB8AA3B;
    ex2.approx.f32 %f10, %f8;
    add.f32 %f10, %f10, 0f3F800000;
    div.rn.f32 %f11, 0f3F800000, %f10;
    mul.f32 %f9, %f6, %f11;

STORE:
    st.global.f32 [%rd10], %f9;
DONE:
    ret;
}
)ptx", R"ptx(
.visible .entry silu_bwd(
    .param .u64 p_dact,
    .param .u64 p_pre,
    .param .u64 p_dpre,
    .param .u32 p_N
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<6>;
    .reg .u64 %rd<8>;
    .reg .f32 %f<10>;

    mov.u32 %r3, %ctaid.x;
    mov.u32 %r4, %ntid.x;
    mov.u32 %r5, %tid.x;
    mad.lo.u32 %r1, %r3, %r4, %r5;
    ld.param.u32 %r2, [p_N];
    setp.ge.u32 %p1, %r1, %r2;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_dact];
    ld.param.u64 %rd2, [p_pre];
    ld.param.u64 %rd3, [p_dpre];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;

    mul.wide.u32 %rd4, %r1, 4;
    add.u64 %rd5, %rd1, %rd4;
    add.u64 %rd6, %rd2, %rd4;
    add.u64 %rd7, %rd3, %rd4;

    ld.global.f32 %f1, [%rd5];
    ld.global.f32 %f2, [%rd6];

    neg.f32 %f3, %f2;
    mul.f32 %f3, %f3, 0f3FB8AA3B;
    ex2.approx.f32 %f4, %f3;
    add.f32 %f4, %f4, 0f3F800000;
    div.rn.f32 %f5, 0f3F800000, %f4;

    sub.f32 %f6, 0f3F800000, %f5;
    mul.f32 %f7, %f2, %f5;
    fma.rn.f32 %f8, %f7, %f6, %f5;
    mul.f32 %f9, %f1, %f8;
    st.global.f32 [%rd7], %f9;
DONE:
    ret;
}

.visible .entry reduce_bias_grad(
    .param .u64 p_dY,
    .param .u64 p_db,
    .param .u32 p_batch,
    .param .u32 p_N
)
{
    .reg .pred %p<3>;
    .reg .u32 %r<8>;
    .reg .u64 %rd<8>;
    .reg .f32 %f<3>;

    mov.u32 %r5, %ctaid.x;
    mov.u32 %r6, %ntid.x;
    mov.u32 %r7, %tid.x;
    mad.lo.u32 %r1, %r5, %r6, %r7;
    ld.param.u32 %r2, [p_batch];
    ld.param.u32 %r3, [p_N];
    setp.ge.u32 %p1, %r1, %r3;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_dY];
    ld.param.u64 %rd2, [p_db];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;

    mul.wide.u32 %rd3, %r1, 4;
    add.u64 %rd4, %rd1, %rd3;
    mul.wide.u32 %rd5, %r3, 4;

    mov.f32 %f1, 0f00000000;
    mov.u32 %r4, 0;
    setp.eq.u32 %p2, %r2, 0;
    @%p2 bra STORE;
B_LOOP:
    ld.global.f32 %f2, [%rd4];
    add.f32 %f1, %f1, %f2;
    add.u64 %rd4, %rd4, %rd5;
    add.u32 %r4, %r4, 1;
    setp.lt.u32 %p2, %r4, %r2;
    @%p2 bra B_LOOP;
STORE:
    add.u64 %rd6, %rd2, %rd3;
    st.global.f32 [%rd6], %f1;
DONE:
    ret;
}

.visible .entry l2_anchor_and_sq(
    .param .u64 p_theta,
    .param .u64 p_theta0,
    .param .u64 p_grads,
    .param .u32 p_N,
    .param .f32 p_lam,
    .param .u64 p_nsq,
    .param .u64 p_l2,
    .param .u64 p_nan
)
{
    .shared .f32 s_nsq[256];
    .shared .f32 s_l2[256];
    .reg .pred %p<5>;
    .reg .u32 %r<10>;
    .reg .u64 %rd<13>;
    .reg .f32 %f<10>;

    mov.u32 %r1, %tid.x;
    mov.u32 %r8, %ctaid.x;
    mov.u32 %r9, %ntid.x;
    mad.lo.u32 %r2, %r8, %r9, %r1;
    ld.param.u32 %r3, [p_N];

    mov.f32 %f1, 0f00000000;
    mov.f32 %f2, 0f00000000;

    setp.ge.u32 %p1, %r2, %r3;
    @%p1 bra SKIP_LOAD;

    ld.param.u64 %rd1, [p_theta];
    ld.param.u64 %rd2, [p_theta0];
    ld.param.u64 %rd3, [p_grads];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;

    mul.wide.u32 %rd4, %r2, 4;
    add.u64 %rd5, %rd1, %rd4;
    add.u64 %rd6, %rd2, %rd4;
    add.u64 %rd7, %rd3, %rd4;

    ld.global.f32 %f3, [%rd5];
    ld.global.f32 %f4, [%rd6];
    ld.global.f32 %f5, [%rd7];
    ld.param.f32 %f6, [p_lam];

    sub.f32 %f7, %f3, %f4;
    fma.rn.f32 %f5, %f6, %f7, %f5;
    st.global.f32 [%rd7], %f5;

    testp.finite.f32 %p2, %f5;
    @%p2 bra FIN_OK;
    ld.param.u64 %rd8, [p_nan];
    cvta.to.global.u64 %rd8, %rd8;
    st.global.u32 [%rd8], 1;
FIN_OK:
    mul.f32 %f1, %f5, %f5;
    mul.f32 %f8, %f7, %f7;
    mul.f32 %f8, %f8, %f6;
    mul.f32 %f2, %f8, 0f3F000000;

SKIP_LOAD:
    mul.wide.u32 %rd9, %r1, 4;
    mov.u64 %rd10, s_nsq;
    add.u64 %rd10, %rd10, %rd9;
    st.shared.f32 [%rd10], %f1;
    mov.u64 %rd11, s_l2;
    add.u64 %rd11, %rd11, %rd9;
    st.shared.f32 [%rd11], %f2;
    bar.sync 0;

    mov.u32 %r4, 128;
RED_LOOP:
    setp.lt.u32 %p3, %r1, %r4;
    @!%p3 bra RED_NEXT;
    add.u32 %r5, %r1, %r4;
    mul.wide.u32 %rd9, %r5, 4;
    mov.u64 %rd12, s_nsq;
    add.u64 %rd12, %rd12, %rd9;
    ld.shared.f32 %f8, [%rd12];
    add.f32 %f1, %f1, %f8;
    st.shared.f32 [%rd10], %f1;
    mov.u64 %rd12, s_l2;
    add.u64 %rd12, %rd12, %rd9;
    ld.shared.f32 %f9, [%rd12];
    add.f32 %f2, %f2, %f9;
    st.shared.f32 [%rd11], %f2;
RED_NEXT:
    bar.sync 0;
    shr.u32 %r4, %r4, 1;
    setp.gt.u32 %p4, %r4, 0;
    @%p4 bra RED_LOOP;

    setp.ne.u32 %p3, %r1, 0;
    @%p3 bra DONE;
    ld.param.u64 %rd8, [p_nsq];
    cvta.to.global.u64 %rd8, %rd8;
    red.global.add.f32 [%rd8], %f1;
    ld.param.u64 %rd8, [p_l2];
    cvta.to.global.u64 %rd8, %rd8;
    red.global.add.f32 [%rd8], %f2;
DONE:
    ret;
}

.visible .entry adamw_clipped_step(
    .param .u64 p_theta,
    .param .u64 p_grads,
    .param .u64 p_m,
    .param .u64 p_v,
    .param .u32 p_N,
    .param .f32 p_lr,
    .param .f32 p_b1,
    .param .f32 p_b2,
    .param .f32 p_eps,
    .param .f32 p_decay,
    .param .f32 p_inv_bc1,
    .param .f32 p_inv_bc2,
    .param .f32 p_max_norm,
    .param .u64 p_nsq,
    .param .u64 p_nan
)
{
    .reg .pred %p<5>;
    .reg .u32 %r<7>;
    .reg .u64 %rd<12>;
    .reg .f32 %f<22>;

    mov.u32 %r4, %ctaid.x;
    mov.u32 %r5, %ntid.x;
    mov.u32 %r6, %tid.x;
    mad.lo.u32 %r1, %r4, %r5, %r6;
    ld.param.u32 %r2, [p_N];
    setp.ge.u32 %p1, %r1, %r2;
    @%p1 bra DONE;

    ld.param.u64 %rd10, [p_nan];
    cvta.to.global.u64 %rd10, %rd10;
    ld.global.u32 %r3, [%rd10];
    setp.ne.u32 %p2, %r3, 0;
    @%p2 bra DONE;

    ld.param.u64 %rd11, [p_nsq];
    cvta.to.global.u64 %rd11, %rd11;
    ld.global.f32 %f1, [%rd11];
    testp.finite.f32 %p3, %f1;
    @!%p3 bra DONE;

    sqrt.rn.f32 %f2, %f1;
    ld.param.f32 %f3, [p_max_norm];
    mov.f32 %f4, 0f3F800000;
    setp.le.f32 %p4, %f2, %f3;
    @%p4 bra CLIP_OK;
    add.f32 %f5, %f2, 0f2B8CBCCC;
    div.rn.f32 %f4, %f3, %f5;
CLIP_OK:

    ld.param.u64 %rd1, [p_theta];
    ld.param.u64 %rd2, [p_grads];
    ld.param.u64 %rd3, [p_m];
    ld.param.u64 %rd4, [p_v];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;
    cvta.to.global.u64 %rd4, %rd4;

    mul.wide.u32 %rd5, %r1, 4;
    add.u64 %rd6, %rd1, %rd5;
    add.u64 %rd7, %rd2, %rd5;
    add.u64 %rd8, %rd3, %rd5;
    add.u64 %rd9, %rd4, %rd5;

    ld.global.f32 %f6, [%rd6];
    ld.global.f32 %f7, [%rd7];
    mul.f32 %f7, %f7, %f4;
    ld.global.f32 %f8, [%rd8];
    ld.global.f32 %f9, [%rd9];

    ld.param.f32 %f10, [p_lr];
    ld.param.f32 %f11, [p_b1];
    ld.param.f32 %f12, [p_b2];
    ld.param.f32 %f13, [p_eps];
    ld.param.f32 %f14, [p_decay];
    ld.param.f32 %f15, [p_inv_bc1];
    ld.param.f32 %f16, [p_inv_bc2];

    sub.f32 %f17, 0f3F800000, %f11;
    mul.f32 %f18, %f17, %f7;
    fma.rn.f32 %f8, %f11, %f8, %f18;

    sub.f32 %f17, 0f3F800000, %f12;
    mul.f32 %f19, %f7, %f7;
    mul.f32 %f18, %f17, %f19;
    fma.rn.f32 %f9, %f12, %f9, %f18;

    st.global.f32 [%rd8], %f8;
    st.global.f32 [%rd9], %f9;

    mul.f32 %f20, %f8, %f15;
    mul.f32 %f21, %f9, %f16;
    sqrt.rn.f32 %f21, %f21;
    add.f32 %f21, %f21, %f13;
    div.rn.f32 %f20, %f20, %f21;
    mul.f32 %f20, %f10, %f20;
    neg.f32 %f20, %f20;
    fma.rn.f32 %f6, %f6, %f14, %f20;
    st.global.f32 [%rd6], %f6;
DONE:
    ret;
}
)ptx" };

struct CudaApi {
    HMODULE dll = nullptr;
    CUresult(__stdcall* cuInit)(unsigned int) = nullptr;
    CUresult(__stdcall* cuDeviceGetCount)(int*) = nullptr;
    CUresult(__stdcall* cuDeviceGet)(CUdevice*, int) = nullptr;
    CUresult(__stdcall* cuDeviceGetName)(char*, int, CUdevice) = nullptr;
    CUresult(__stdcall* cuDeviceTotalMem_v2)(size_t*, CUdevice) = nullptr;
    CUresult(__stdcall* cuDeviceGetAttribute)(int*, int, CUdevice) = nullptr;
    CUresult(__stdcall* cuCtxCreate_v2)(CUcontext*, unsigned int, CUdevice) = nullptr;
    CUresult(__stdcall* cuCtxDestroy_v2)(CUcontext) = nullptr;
    CUresult(__stdcall* cuMemGetInfo_v2)(size_t*, size_t*) = nullptr;
    CUresult(__stdcall* cuMemAlloc_v2)(CUdeviceptr*, size_t) = nullptr;
    CUresult(__stdcall* cuMemFree_v2)(CUdeviceptr) = nullptr;
    CUresult(__stdcall* cuMemcpyHtoD_v2)(CUdeviceptr, const void*, size_t) = nullptr;
    CUresult(__stdcall* cuMemcpyDtoH_v2)(void*, CUdeviceptr, size_t) = nullptr;
    CUresult(__stdcall* cuMemcpyHtoDAsync_v2)(CUdeviceptr, const void*, size_t, CUstream) = nullptr;
    CUresult(__stdcall* cuMemcpyDtoHAsync_v2)(void*, CUdeviceptr, size_t, CUstream) = nullptr;
    CUresult(__stdcall* cuMemcpyDtoDAsync_v2)(CUdeviceptr, CUdeviceptr, size_t, CUstream) = nullptr;
    CUresult(__stdcall* cuMemsetD32Async)(CUdeviceptr, unsigned int, size_t, CUstream) = nullptr;
    CUresult(__stdcall* cuModuleLoadData)(CUmodule*, const void*) = nullptr;
    CUresult(__stdcall* cuModuleLoadDataEx)(CUmodule*, const void*, unsigned int, int*, void**) = nullptr;
    CUresult(__stdcall* cuModuleUnload)(CUmodule) = nullptr;
    CUresult(__stdcall* cuModuleGetFunction)(CUfunction*, CUmodule, const char*) = nullptr;
    CUresult(__stdcall* cuStreamCreate)(CUstream*, unsigned int) = nullptr;
    CUresult(__stdcall* cuStreamDestroy_v2)(CUstream) = nullptr;
    CUresult(__stdcall* cuStreamSynchronize)(CUstream) = nullptr;
    CUresult(__stdcall* cuEventCreate)(CUevent*, unsigned int) = nullptr;
    CUresult(__stdcall* cuEventDestroy_v2)(CUevent) = nullptr;
    CUresult(__stdcall* cuEventRecord)(CUevent, CUstream) = nullptr;
    CUresult(__stdcall* cuEventSynchronize)(CUevent) = nullptr;
    CUresult(__stdcall* cuEventElapsedTime)(float*, CUevent, CUevent) = nullptr;
    CUresult(__stdcall* cuLaunchKernel)(
        CUfunction, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int, unsigned int,
        unsigned int, CUstream, void**, void**
    ) = nullptr;
    CUresult(__stdcall* cuGetErrorName)(CUresult, const char**) = nullptr;
    CUresult(__stdcall* cuGetErrorString)(CUresult, const char**) = nullptr;

    bool load() {
        dll = LoadLibraryA("nvcuda.dll");
        if (!dll) return false;
        auto gp = [&](auto& fn, const char* name) -> bool {
            fn = reinterpret_cast<std::decay_t<decltype(fn)>>(GetProcAddress(dll, name));
            return fn != nullptr;
        };
        return gp(cuInit, "cuInit") &&
               gp(cuDeviceGetCount, "cuDeviceGetCount") &&
               gp(cuDeviceGet, "cuDeviceGet") &&
               gp(cuDeviceGetName, "cuDeviceGetName") &&
               gp(cuDeviceTotalMem_v2, "cuDeviceTotalMem_v2") &&
               gp(cuDeviceGetAttribute, "cuDeviceGetAttribute") &&
               gp(cuCtxCreate_v2, "cuCtxCreate_v2") &&
               gp(cuCtxDestroy_v2, "cuCtxDestroy_v2") &&
               gp(cuMemGetInfo_v2, "cuMemGetInfo_v2") &&
               gp(cuMemAlloc_v2, "cuMemAlloc_v2") &&
               gp(cuMemFree_v2, "cuMemFree_v2") &&
               gp(cuMemcpyHtoD_v2, "cuMemcpyHtoD_v2") &&
               gp(cuMemcpyDtoH_v2, "cuMemcpyDtoH_v2") &&
               gp(cuMemcpyHtoDAsync_v2, "cuMemcpyHtoDAsync_v2") &&
               gp(cuMemcpyDtoHAsync_v2, "cuMemcpyDtoHAsync_v2") &&
               gp(cuMemcpyDtoDAsync_v2, "cuMemcpyDtoDAsync_v2") &&
               gp(cuMemsetD32Async, "cuMemsetD32Async") &&
               gp(cuModuleLoadData, "cuModuleLoadData") &&
               gp(cuModuleLoadDataEx, "cuModuleLoadDataEx") &&
               gp(cuModuleUnload, "cuModuleUnload") &&
               gp(cuModuleGetFunction, "cuModuleGetFunction") &&
               gp(cuStreamCreate, "cuStreamCreate") &&
               gp(cuStreamDestroy_v2, "cuStreamDestroy_v2") &&
               gp(cuStreamSynchronize, "cuStreamSynchronize") &&
               gp(cuEventCreate, "cuEventCreate") &&
               gp(cuEventDestroy_v2, "cuEventDestroy_v2") &&
               gp(cuEventRecord, "cuEventRecord") &&
               gp(cuEventSynchronize, "cuEventSynchronize") &&
               gp(cuEventElapsedTime, "cuEventElapsedTime") &&
               gp(cuLaunchKernel, "cuLaunchKernel") &&
               gp(cuGetErrorName, "cuGetErrorName") &&
               gp(cuGetErrorString, "cuGetErrorString");
    }

    std::string err_str(CUresult) const { return ""; }
};

struct CudaDeviceInfo {
    int ordinal = 0;
    std::string name;
    size_t total_vram_bytes = 0;
    int sm_count = 0;
    int cc_major = 0;
    int cc_minor = 0;
};

struct CudaBackend {
    CudaApi api;
    CUdevice dev = 0;
    CUcontext ctx = nullptr;
    CUmodule mod = nullptr;
    CUstream stream = nullptr;
    CudaDeviceInfo info{};

    CUfunction fn_sgemm = nullptr;
    CUfunction fn_sgemm_tn = nullptr;
    CUfunction fn_sgemm_nt = nullptr;
    CUfunction fn_im2col = nullptr;
    CUfunction fn_slice_t = nullptr;
    CUfunction fn_concat2 = nullptr;
    CUfunction fn_gru = nullptr;
    CUfunction fn_out_act = nullptr;
    CUfunction fn_mse_grad = nullptr;
    CUfunction fn_silu_bwd = nullptr;
    CUfunction fn_reduce_b = nullptr;
    CUfunction fn_l2_sq = nullptr;
    CUfunction fn_adamw = nullptr;

    CUdeviceptr d_bb = 0;
    CUdeviceptr d_anchor = 0;
    CUdeviceptr d_theta = 0;
    CUdeviceptr d_grads = 0;
    CUdeviceptr d_m = 0;
    CUdeviceptr d_v = 0;

    CUdeviceptr d_xseq = 0;
    CUdeviceptr d_xctx = 0;
    CUdeviceptr d_target = 0;
    CUdeviceptr d_ctx1 = 0;
    CUdeviceptr d_hctx = 0;
    CUdeviceptr d_cols0 = 0;
    CUdeviceptr d_cols1 = 0;
    CUdeviceptr d_c1 = 0;
    CUdeviceptr d_c2 = 0;
    CUdeviceptr d_c3 = 0;
    CUdeviceptr d_hseq = 0;
    CUdeviceptr d_gru_xt = 0;
    CUdeviceptr d_gru_gx = 0;
    CUdeviceptr d_gru_gh = 0;
    CUdeviceptr d_gru_h0 = 0;
    CUdeviceptr d_gru_h1 = 0;
    CUdeviceptr d_z0 = 0;
    CUdeviceptr d_pre1 = 0;
    CUdeviceptr d_a1 = 0;
    CUdeviceptr d_pre2 = 0;
    CUdeviceptr d_a2 = 0;
    CUdeviceptr d_pre3 = 0;
    CUdeviceptr d_a3 = 0;
    CUdeviceptr d_raw_out = 0;
    CUdeviceptr d_pred_out = 0;

    CUdeviceptr d_draw = 0;
    CUdeviceptr d_da3 = 0;
    CUdeviceptr d_dpre3 = 0;
    CUdeviceptr d_da2 = 0;
    CUdeviceptr d_dpre2 = 0;
    CUdeviceptr d_da1 = 0;
    CUdeviceptr d_dpre1 = 0;
    CUdeviceptr d_scalars = 0;

    uint32_t max_batch = 0;
    size_t allocated_bytes = 0;
    strafenet::ArchType loaded_arch = strafenet::ArchType::TCN_ResMLP;
    uint64_t opt_step = 0;
    uint64_t skipped_nonfinite = 0;

    bool init(int ordinal = 0) {
        if (!api.load()) return false;
        if (api.cuInit(0) != CUDA_SUCCESS) { destroy(); return false; }
        int count = 0;
        if (api.cuDeviceGetCount(&count) != CUDA_SUCCESS || count <= ordinal) { destroy(); return false; }
        if (api.cuDeviceGet(&dev, ordinal) != CUDA_SUCCESS) { destroy(); return false; }

        char name_buf[256] = {};
        api.cuDeviceGetName(name_buf, sizeof(name_buf), dev);
        info.ordinal = ordinal;
        info.name = name_buf;
        api.cuDeviceTotalMem_v2(&info.total_vram_bytes, dev);
        api.cuDeviceGetAttribute(&info.sm_count, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, dev);
        api.cuDeviceGetAttribute(&info.cc_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
        api.cuDeviceGetAttribute(&info.cc_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);

        CUresult r = api.cuCtxCreate_v2(&ctx, 0, dev);
        if (r != CUDA_SUCCESS) { destroy(); return false; }
        char err_log[4096] = {};
        int opts[2] = { 5, 6 };
        void* opt_vals[2] = { err_log, reinterpret_cast<void*>(static_cast<uintptr_t>(sizeof(err_log))) };
        std::string ptx;
        for (const char* part : kStrafeNetPtxParts)
            ptx += part;
        r = api.cuModuleLoadDataEx(&mod, ptx.c_str(), 2, opts, opt_vals);
        if (r != CUDA_SUCCESS) { destroy(); return false; }
        auto gf = [&](CUfunction& fn, const char* name) -> bool {
            CUresult gr = api.cuModuleGetFunction(&fn, mod, name);
            if (gr != CUDA_SUCCESS) {
                return false;
            }
            return true;
        };
        if (!gf(fn_sgemm, "sgemm_bias_skip_act") ||
            !gf(fn_sgemm_tn, "sgemm_tn") ||
            !gf(fn_sgemm_nt, "sgemm_nt") ||
            !gf(fn_im2col, "im2col_causal_conv1d") ||
            !gf(fn_slice_t, "slice_timestep") ||
            !gf(fn_concat2, "concat_2") ||
            !gf(fn_gru, "gru_gates") ||
            !gf(fn_out_act, "strafenet_out_act") ||
            !gf(fn_mse_grad, "strafenet_mse_out_grad") ||
            !gf(fn_silu_bwd, "silu_bwd") ||
            !gf(fn_reduce_b, "reduce_bias_grad") ||
            !gf(fn_l2_sq, "l2_anchor_and_sq") ||
            !gf(fn_adamw, "adamw_clipped_step")) {
            return false;
        }
        if (api.cuStreamCreate(&stream, 0) != CUDA_SUCCESS) return false;
        return true;
    }

    CUdeviceptr alloc_bytes(size_t bytes) {
        CUdeviceptr p = 0;
        if (api.cuMemAlloc_v2(&p, bytes) == CUDA_SUCCESS) {
            allocated_bytes += bytes;
            return p;
        }
        return 0;
    }

    void free_model_buffers() {
        auto fr = [&](CUdeviceptr& p) { if (p) { api.cuMemFree_v2(p); p = 0; } };
        fr(d_bb); fr(d_anchor); fr(d_theta); fr(d_grads); fr(d_m); fr(d_v);
        fr(d_xseq); fr(d_xctx); fr(d_target); fr(d_ctx1); fr(d_hctx);
        fr(d_cols0); fr(d_cols1); fr(d_c1); fr(d_c2); fr(d_c3); fr(d_hseq);
        fr(d_gru_xt); fr(d_gru_gx); fr(d_gru_gh); fr(d_gru_h0); fr(d_gru_h1);
        fr(d_z0); fr(d_pre1); fr(d_a1); fr(d_pre2); fr(d_a2); fr(d_pre3); fr(d_a3); fr(d_raw_out); fr(d_pred_out);
        fr(d_draw); fr(d_da3); fr(d_dpre3); fr(d_da2); fr(d_dpre2); fr(d_da1); fr(d_dpre1); fr(d_scalars);
        allocated_bytes = 0;
    }

    bool upload_model(const strafenet::StrafeNetWeights& w, uint32_t batch_cap) {
        using D = strafenet::ModelDims;
        free_model_buffers();
        max_batch = batch_cap;
        loaded_arch = w.arch;
        opt_step = 0;
        skipped_nonfinite = 0;

        d_bb = alloc_bytes(w.backbone.size() * sizeof(float));
        d_anchor = alloc_bytes(D::PLASTIC_TOTAL * sizeof(float));
        d_theta = alloc_bytes(D::PLASTIC_TOTAL * sizeof(float));
        d_grads = alloc_bytes(D::PLASTIC_TOTAL * sizeof(float));
        d_m = alloc_bytes(D::PLASTIC_TOTAL * sizeof(float));
        d_v = alloc_bytes(D::PLASTIC_TOTAL * sizeof(float));

        d_xseq = alloc_bytes(batch_cap * D::SEQ_LEN * D::D_SEQ * sizeof(float));
        d_xctx = alloc_bytes(batch_cap * D::D_CTX * sizeof(float));
        d_target = alloc_bytes(batch_cap * D::OUT_TOTAL * sizeof(float));
        d_ctx1 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_hctx = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_cols0 = alloc_bytes(batch_cap * D::SEQ_LEN * 3 * D::D_SEQ * sizeof(float));
        d_cols1 = alloc_bytes(batch_cap * D::SEQ_LEN * 3 * D::HIDDEN * sizeof(float));
        d_c1 = alloc_bytes(batch_cap * D::SEQ_LEN * D::HIDDEN * sizeof(float));
        d_c2 = alloc_bytes(batch_cap * D::SEQ_LEN * D::HIDDEN * sizeof(float));
        d_c3 = alloc_bytes(batch_cap * D::SEQ_LEN * D::HIDDEN * sizeof(float));
        d_hseq = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_gru_xt = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_gru_gx = alloc_bytes(batch_cap * 3 * D::HIDDEN * sizeof(float));
        d_gru_gh = alloc_bytes(batch_cap * 3 * D::HIDDEN * sizeof(float));
        d_gru_h0 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_gru_h1 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_z0 = alloc_bytes(batch_cap * D::TRUNK_H * sizeof(float));
        d_pre1 = alloc_bytes(batch_cap * D::TRUNK_H * sizeof(float));
        d_a1 = alloc_bytes(batch_cap * D::TRUNK_H * sizeof(float));
        d_pre2 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_a2 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_pre3 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_a3 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_raw_out = alloc_bytes(batch_cap * D::OUT_TOTAL * sizeof(float));
        d_pred_out = alloc_bytes(batch_cap * D::OUT_TOTAL * sizeof(float));
        d_draw = alloc_bytes(batch_cap * D::OUT_TOTAL * sizeof(float));
        d_da3 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_dpre3 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_da2 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_dpre2 = alloc_bytes(batch_cap * D::HIDDEN * sizeof(float));
        d_da1 = alloc_bytes(batch_cap * D::TRUNK_H * sizeof(float));
        d_dpre1 = alloc_bytes(batch_cap * D::TRUNK_H * sizeof(float));
        d_scalars = alloc_bytes(16 * sizeof(uint32_t));

        if (!d_bb || !d_anchor || !d_theta || !d_grads || !d_m || !d_v || !d_scalars || !d_pred_out || !d_dpre1) return false;

        if (api.cuMemcpyHtoD_v2(d_bb, w.backbone.data(), w.backbone.size() * 4) != CUDA_SUCCESS) return false;
        if (api.cuMemcpyHtoD_v2(d_anchor, w.anchor_theta0.data(), D::PLASTIC_TOTAL * 4) != CUDA_SUCCESS) return false;
        if (api.cuMemcpyHtoD_v2(d_theta, w.plastic_theta.data(), D::PLASTIC_TOTAL * 4) != CUDA_SUCCESS) return false;
        api.cuMemsetD32Async(d_m, 0, D::PLASTIC_TOTAL, stream);
        api.cuMemsetD32Async(d_v, 0, D::PLASTIC_TOTAL, stream);
        api.cuStreamSynchronize(stream);
        return true;
    }

    bool sync_plastic_weights(const strafenet::StrafeNetWeights& w) {
        return api.cuMemcpyHtoD_v2(d_theta, w.plastic_theta.data(), strafenet::ModelDims::PLASTIC_TOTAL * 4) == CUDA_SUCCESS;
    }

    bool download_plastic_weights(strafenet::StrafeNetWeights& w) {
        return api.cuMemcpyDtoH_v2(w.plastic_theta.data(), d_theta, strafenet::ModelDims::PLASTIC_TOTAL * 4) == CUDA_SUCCESS;
    }

    void reset_optimizer() {
        opt_step = 0;
        skipped_nonfinite = 0;
        api.cuMemsetD32Async(d_m, 0, strafenet::ModelDims::PLASTIC_TOTAL, stream);
        api.cuMemsetD32Async(d_v, 0, strafenet::ModelDims::PLASTIC_TOTAL, stream);
        api.cuStreamSynchronize(stream);
    }

    void launch_sgemm(CUdeviceptr A, CUdeviceptr B, CUdeviceptr bias, CUdeviceptr skip, CUdeviceptr pre, CUdeviceptr act, uint32_t M, uint32_t N, uint32_t K, uint32_t silu) {
        uint32_t bx = (M == 1) ? 64u : 32u;
        uint32_t by = (M == 1) ? 1u : 8u;
        uint32_t gx = (N + bx - 1) / bx;
        uint32_t gy = (M + by - 1) / by;
        void* params[] = { &A, &B, &bias, &skip, &pre, &act, &M, &N, &K, &silu };
        api.cuLaunchKernel(fn_sgemm, gx, gy, 1, bx, by, 1, 0, stream, params, nullptr);
    }

    void launch_sgemm_tn(CUdeviceptr A, CUdeviceptr B, CUdeviceptr C, uint32_t I, uint32_t J, uint32_t K) {
        uint32_t bx = 32u, by = 8u;
        void* params[] = { &A, &B, &C, &I, &J, &K };
        api.cuLaunchKernel(fn_sgemm_tn, (J + bx - 1) / bx, (I + by - 1) / by, 1, bx, by, 1, 0, stream, params, nullptr);
    }

    void launch_sgemm_nt(CUdeviceptr A, CUdeviceptr B, CUdeviceptr C, uint32_t M, uint32_t N, uint32_t K) {
        uint32_t bx = 32u, by = 8u;
        void* params[] = { &A, &B, &C, &M, &N, &K };
        api.cuLaunchKernel(fn_sgemm_nt, (N + bx - 1) / bx, (M + by - 1) / by, 1, bx, by, 1, 0, stream, params, nullptr);
    }

    void forward_async(const strafenet::StrafeNetWeights& w, uint32_t batch) {
        using D = strafenet::ModelDims;
        auto bb = [&](size_t off) -> CUdeviceptr { return d_bb + off * sizeof(float); };
        auto pt = [&](size_t off) -> CUdeviceptr { return d_theta + off * sizeof(float); };

        launch_sgemm(d_xctx, bb(w.off_ctx_w1), bb(w.off_ctx_b1), 0, 0, d_ctx1, batch, D::HIDDEN, D::D_CTX, 1);
        launch_sgemm(d_ctx1, bb(w.off_ctx_w2), bb(w.off_ctx_b2), 0, 0, d_hctx, batch, D::HIDDEN, D::HIDDEN, 1);

        if (w.arch == strafenet::ArchType::TCN_ResMLP) {
            uint32_t seq = D::SEQ_LEN, cin0 = D::D_SEQ, d1 = 1, d2 = 2, d4 = 4, hid = D::HIDDEN;
            uint32_t n0 = batch * seq * 3 * cin0;
            void* p0[] = { &d_xseq, &d_cols0, &batch, &seq, &cin0, &d1 };
            api.cuLaunchKernel(fn_im2col, (n0 + 255) / 256, 1, 1, 256, 1, 1, 0, stream, p0, nullptr);
            launch_sgemm(d_cols0, bb(w.off_tcn_w1), bb(w.off_tcn_b1), 0, 0, d_c1, batch * seq, hid, 3 * cin0, 1);

            uint32_t n1 = batch * seq * 3 * hid;
            void* p1[] = { &d_c1, &d_cols1, &batch, &seq, &hid, &d2 };
            api.cuLaunchKernel(fn_im2col, (n1 + 255) / 256, 1, 1, 256, 1, 1, 0, stream, p1, nullptr);
            launch_sgemm(d_cols1, bb(w.off_tcn_w2), bb(w.off_tcn_b2), d_c1, 0, d_c2, batch * seq, hid, 3 * hid, 1);

            void* p2[] = { &d_c2, &d_cols1, &batch, &seq, &hid, &d4 };
            api.cuLaunchKernel(fn_im2col, (n1 + 255) / 256, 1, 1, 256, 1, 1, 0, stream, p2, nullptr);
            launch_sgemm(d_cols1, bb(w.off_tcn_w3), bb(w.off_tcn_b3), d_c2, 0, d_c3, batch * seq, hid, 3 * hid, 1);

            uint32_t t_last = seq - 1;
            uint32_t ns = batch * hid;
            void* ps[] = { &d_c3, &d_hseq, &batch, &seq, &hid, &t_last };
            api.cuLaunchKernel(fn_slice_t, (ns + 127) / 128, 1, 1, 128, 1, 1, 0, stream, ps, nullptr);
        } else {
            uint32_t seq = D::SEQ_LEN, cin0 = D::D_SEQ, d1 = 1, hid = D::HIDDEN, h3 = 3 * D::HIDDEN;
            uint32_t n0 = batch * seq * 3 * cin0;
            void* p0[] = { &d_xseq, &d_cols0, &batch, &seq, &cin0, &d1 };
            api.cuLaunchKernel(fn_im2col, (n0 + 255) / 256, 1, 1, 256, 1, 1, 0, stream, p0, nullptr);
            launch_sgemm(d_cols0, bb(w.off_gru_conv_w), bb(w.off_gru_conv_b), 0, 0, d_c1, batch * seq, hid, 3 * cin0, 1);

            api.cuMemsetD32Async(d_gru_h0, 0, batch * hid, stream);
            CUdeviceptr cur = d_gru_h0;
            CUdeviceptr nxt = d_gru_h1;
            uint32_t ns = batch * hid;
            for (uint32_t t = 0; t < seq; ++t) {
                void* ps[] = { &d_c1, &d_gru_xt, &batch, &seq, &hid, &t };
                api.cuLaunchKernel(fn_slice_t, (ns + 127) / 128, 1, 1, 128, 1, 1, 0, stream, ps, nullptr);
                launch_sgemm(d_gru_xt, bb(w.off_gru_w_ih), bb(w.off_gru_b_ih), 0, 0, d_gru_gx, batch, h3, hid, 0);
                launch_sgemm(cur, bb(w.off_gru_w_hh), bb(w.off_gru_b_hh), 0, 0, d_gru_gh, batch, h3, hid, 0);
                void* pg[] = { &d_gru_gx, &d_gru_gh, &cur, &nxt, &batch, &hid };
                api.cuLaunchKernel(fn_gru, (ns + 127) / 128, 1, 1, 128, 1, 1, 0, stream, pg, nullptr);
                std::swap(cur, nxt);
            }
            api.cuMemcpyDtoDAsync_v2(d_hseq, cur, batch * hid * 4, stream);
        }

        uint32_t hid = D::HIDDEN;
        uint32_t nc = batch * D::TRUNK_H;
        void* pc[] = { &d_hseq, &d_hctx, &d_z0, &batch, &hid };
        api.cuLaunchKernel(fn_concat2, (nc + 255) / 256, 1, 1, 256, 1, 1, 0, stream, pc, nullptr);

        launch_sgemm(d_z0, pt(w.off_tr_w1), pt(w.off_tr_b1), 0, d_pre1, d_a1, batch, D::TRUNK_H, D::TRUNK_H, 1);
        launch_sgemm(d_a1, pt(w.off_tr_w2), pt(w.off_tr_b2), 0, d_pre2, d_a2, batch, D::HIDDEN, D::TRUNK_H, 1);
        launch_sgemm(d_a2, pt(w.off_tr_w3), pt(w.off_tr_b3), 0, d_pre3, d_a3, batch, D::HIDDEN, D::HIDDEN, 1);
        launch_sgemm(d_a3, pt(w.off_tr_w4), pt(w.off_tr_b4), 0, d_raw_out, 0, batch, D::OUT_TOTAL, D::HIDDEN, 0);

        uint32_t nout = batch * D::OUT_TOTAL;
        void* po[] = { &d_raw_out, &d_pred_out, &nout };
        api.cuLaunchKernel(fn_out_act, (nout + 255) / 256, 1, 1, 256, 1, 1, 0, stream, po, nullptr);
    }

    void backward_and_adamw_async(
        const strafenet::StrafeNetWeights& w,
        uint32_t batch,
        const strafenet::TrainConfig& cfg,
        float inv_bc1,
        float inv_bc2
    ) {
        using D = strafenet::ModelDims;
        auto pt = [&](size_t off) -> CUdeviceptr { return d_theta + off * sizeof(float); };
        auto gr = [&](size_t off) -> CUdeviceptr { return d_grads + off * sizeof(float); };

        CUdeviceptr d_mse_sum = d_scalars + 0;
        CUdeviceptr d_nsq = d_scalars + 4;
        CUdeviceptr d_l2 = d_scalars + 8;
        CUdeviceptr d_nan = d_scalars + 12;
        api.cuMemsetD32Async(d_scalars, 0, 4, stream);

        uint32_t nout = batch * D::OUT_TOTAL;
        void* pm[] = { &d_pred_out, &d_target, &d_raw_out, &d_draw, &d_mse_sum, &nout };
        api.cuLaunchKernel(fn_mse_grad, (nout + 255) / 256, 1, 1, 256, 1, 1, 0, stream, pm, nullptr);

        launch_sgemm_tn(d_a3, d_draw, gr(w.off_tr_w4), D::HIDDEN, D::OUT_TOTAL, batch);
        {
            uint32_t n4 = D::OUT_TOTAL;
            CUdeviceptr db4 = gr(w.off_tr_b4);
            void* pb[] = { &d_draw, &db4, &batch, &n4 };
            api.cuLaunchKernel(fn_reduce_b, (n4 + 127) / 128, 1, 1, 128, 1, 1, 0, stream, pb, nullptr);
        }

        launch_sgemm_nt(d_draw, pt(w.off_tr_w4), d_da3, batch, D::HIDDEN, D::OUT_TOTAL);
        {
            uint32_t cnt = batch * D::HIDDEN;
            void* ps[] = { &d_da3, &d_pre3, &d_dpre3, &cnt };
            api.cuLaunchKernel(fn_silu_bwd, (cnt + 255) / 256, 1, 1, 256, 1, 1, 0, stream, ps, nullptr);
        }

        launch_sgemm_tn(d_a2, d_dpre3, gr(w.off_tr_w3), D::HIDDEN, D::HIDDEN, batch);
        {
            uint32_t n3 = D::HIDDEN;
            CUdeviceptr db3 = gr(w.off_tr_b3);
            void* pb[] = { &d_dpre3, &db3, &batch, &n3 };
            api.cuLaunchKernel(fn_reduce_b, (n3 + 127) / 128, 1, 1, 128, 1, 1, 0, stream, pb, nullptr);
        }

        launch_sgemm_nt(d_dpre3, pt(w.off_tr_w3), d_da2, batch, D::HIDDEN, D::HIDDEN);
        {
            uint32_t cnt = batch * D::HIDDEN;
            void* ps[] = { &d_da2, &d_pre2, &d_dpre2, &cnt };
            api.cuLaunchKernel(fn_silu_bwd, (cnt + 255) / 256, 1, 1, 256, 1, 1, 0, stream, ps, nullptr);
        }

        launch_sgemm_tn(d_a1, d_dpre2, gr(w.off_tr_w2), D::TRUNK_H, D::HIDDEN, batch);
        {
            uint32_t n2 = D::HIDDEN;
            CUdeviceptr db2 = gr(w.off_tr_b2);
            void* pb[] = { &d_dpre2, &db2, &batch, &n2 };
            api.cuLaunchKernel(fn_reduce_b, (n2 + 127) / 128, 1, 1, 128, 1, 1, 0, stream, pb, nullptr);
        }

        launch_sgemm_nt(d_dpre2, pt(w.off_tr_w2), d_da1, batch, D::TRUNK_H, D::HIDDEN);
        {
            uint32_t cnt = batch * D::TRUNK_H;
            void* ps[] = { &d_da1, &d_pre1, &d_dpre1, &cnt };
            api.cuLaunchKernel(fn_silu_bwd, (cnt + 255) / 256, 1, 1, 256, 1, 1, 0, stream, ps, nullptr);
        }

        launch_sgemm_tn(d_z0, d_dpre1, gr(w.off_tr_w1), D::TRUNK_H, D::TRUNK_H, batch);
        {
            uint32_t n1 = D::TRUNK_H;
            CUdeviceptr db1 = gr(w.off_tr_b1);
            void* pb[] = { &d_dpre1, &db1, &batch, &n1 };
            api.cuLaunchKernel(fn_reduce_b, (n1 + 127) / 128, 1, 1, 128, 1, 1, 0, stream, pb, nullptr);
        }

        uint32_t n_plastic = D::PLASTIC_TOTAL;
        float lam = cfg.lambda_anchor;
        void* pl[] = { &d_theta, &d_anchor, &d_grads, &n_plastic, &lam, &d_nsq, &d_l2, &d_nan };
        api.cuLaunchKernel(fn_l2_sq, (n_plastic + 255) / 256, 1, 1, 256, 1, 1, 0, stream, pl, nullptr);

        float lr = cfg.lr, b1 = cfg.beta1, b2 = cfg.beta2, eps = cfg.eps;
        float decay = 1.0f - cfg.lr * cfg.weight_decay;
        float max_norm = cfg.max_grad_norm;
        void* pa[] = { &d_theta, &d_grads, &d_m, &d_v, &n_plastic, &lr, &b1, &b2, &eps, &decay, &inv_bc1, &inv_bc2, &max_norm, &d_nsq, &d_nan };
        api.cuLaunchKernel(fn_adamw, (n_plastic + 255) / 256, 1, 1, 256, 1, 1, 0, stream, pa, nullptr);
    }

    void forward_sync(const strafenet::StrafeNetWeights& w, const float* x_seq, const float* x_ctx, float* out, uint32_t batch) {
        using D = strafenet::ModelDims;
        api.cuMemcpyHtoDAsync_v2(d_xseq, x_seq, batch * D::SEQ_LEN * D::D_SEQ * 4, stream);
        api.cuMemcpyHtoDAsync_v2(d_xctx, x_ctx, batch * D::D_CTX * 4, stream);
        forward_async(w, batch);
        api.cuMemcpyDtoHAsync_v2(out, d_pred_out, batch * D::OUT_TOTAL * 4, stream);
        api.cuStreamSynchronize(stream);
    }

    strafenet::TrainStepMetrics train_step_sync(
        const strafenet::StrafeNetWeights& w,
        const float* x_seq,
        const float* x_ctx,
        const float* target,
        uint32_t batch,
        const strafenet::TrainConfig& cfg
    ) {
        using D = strafenet::ModelDims;
        uint64_t next_step = opt_step + 1;
        float inv_bc1 = 1.0f / (1.0f - std::pow(cfg.beta1, static_cast<float>(next_step)));
        float inv_bc2 = 1.0f / (1.0f - std::pow(cfg.beta2, static_cast<float>(next_step)));

        api.cuMemcpyHtoDAsync_v2(d_xseq, x_seq, batch * D::SEQ_LEN * D::D_SEQ * 4, stream);
        api.cuMemcpyHtoDAsync_v2(d_xctx, x_ctx, batch * D::D_CTX * 4, stream);
        api.cuMemcpyHtoDAsync_v2(d_target, target, batch * D::OUT_TOTAL * 4, stream);
        forward_async(w, batch);
        backward_and_adamw_async(w, batch, cfg, inv_bc1, inv_bc2);

        uint32_t host_scalars[4] = {};
        api.cuMemcpyDtoHAsync_v2(host_scalars, d_scalars, 16, stream);
        api.cuStreamSynchronize(stream);

        float mse_sum = 0.0f, norm_sq = 0.0f, l2_loss = 0.0f;
        std::memcpy(&mse_sum, &host_scalars[0], 4);
        std::memcpy(&norm_sq, &host_scalars[1], 4);
        std::memcpy(&l2_loss, &host_scalars[2], 4);
        uint32_t nan_flag = host_scalars[3];

        strafenet::TrainStepMetrics m{};
        m.mse_loss = mse_sum / static_cast<float>(batch * D::OUT_TOTAL);
        m.l2_anchor_loss = l2_loss;
        m.total_loss = m.mse_loss + l2_loss;
        m.grad_norm = std::sqrt(norm_sq);
        if (nan_flag != 0 || !std::isfinite(norm_sq) || !std::isfinite(m.mse_loss)) {
            skipped_nonfinite++;
            m.applied = false;
        } else {
            opt_step = next_step;
            m.applied = true;
        }
        return m;
    }

    void destroy() {
        free_model_buffers();
        if (stream) { api.cuStreamDestroy_v2(stream); stream = nullptr; }
        if (mod) { api.cuModuleUnload(mod); mod = nullptr; }
        if (ctx) { api.cuCtxDestroy_v2(ctx); ctx = nullptr; }
        if (api.dll) { FreeLibrary(api.dll); api.dll = nullptr; }
    }
};

}


namespace strafenet_dml {

using Microsoft::WRL::ComPtr;

struct DxgiAdapterInfo {
    uint32_t index = 0;
    std::wstring description;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    size_t dedicated_vram = 0;
    bool is_software = false;
};

struct DmlApi {
    HMODULE h_dxgi = nullptr;
    HMODULE h_d3d12 = nullptr;
    HMODULE h_dml = nullptr;

    HRESULT(WINAPI* fn_CreateDXGIFactory1)(REFIID, void**) = nullptr;
    HRESULT(WINAPI* fn_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**) = nullptr;
    HRESULT(WINAPI* fn_DMLCreateDevice)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**) = nullptr;

    bool load() {
        h_dxgi = LoadLibraryA("dxgi.dll");
        h_d3d12 = LoadLibraryA("d3d12.dll");
        h_dml = LoadLibraryA("directml.dll");
        if (!h_dxgi || !h_d3d12 || !h_dml) return false;
        fn_CreateDXGIFactory1 = reinterpret_cast<decltype(fn_CreateDXGIFactory1)>(GetProcAddress(h_dxgi, "CreateDXGIFactory1"));
        fn_D3D12CreateDevice = reinterpret_cast<decltype(fn_D3D12CreateDevice)>(GetProcAddress(h_d3d12, "D3D12CreateDevice"));
        fn_DMLCreateDevice = reinterpret_cast<decltype(fn_DMLCreateDevice)>(GetProcAddress(h_dml, "DMLCreateDevice"));
        return fn_CreateDXGIFactory1 && fn_D3D12CreateDevice && fn_DMLCreateDevice;
    }

    void unload() {
        if (h_dml) { FreeLibrary(h_dml); h_dml = nullptr; }
        if (h_d3d12) { FreeLibrary(h_d3d12); h_d3d12 = nullptr; }
        if (h_dxgi) { FreeLibrary(h_dxgi); h_dxgi = nullptr; }
    }
};

inline std::vector<DxgiAdapterInfo> enumerate_dxgi_adapters(const DmlApi& api) {
    std::vector<DxgiAdapterInfo> out;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(api.fn_CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return out;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            DxgiAdapterInfo info{};
            info.index = i;
            info.description = desc.Description;
            info.vendor_id = desc.VendorId;
            info.device_id = desc.DeviceId;
            info.dedicated_vram = desc.DedicatedVideoMemory;
            info.is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            out.push_back(info);
        }
        adapter.Reset();
    }
    return out;
}

inline uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

struct DmlBuffer {
    ComPtr<ID3D12Resource> res;
    uint64_t size = 0;
};

struct Tensor4D {
    UINT sizes[4] = { 1, 1, 1, 1 };
    UINT strides[4] = { 0, 0, 0, 0 };
    DML_BUFFER_TENSOR_DESC bdesc{};
    DML_TENSOR_DESC desc{};

    void set(UINT d0, UINT d1, UINT d2, UINT d3) {
        sizes[0] = d0; sizes[1] = d1; sizes[2] = d2; sizes[3] = d3;
        bdesc.DataType = DML_TENSOR_DATA_TYPE_FLOAT32;
        bdesc.Flags = DML_TENSOR_FLAG_NONE;
        bdesc.DimensionCount = 4;
        bdesc.Sizes = sizes;
        bdesc.Strides = nullptr;
        bdesc.TotalTensorSizeInBytes = static_cast<UINT64>(d0) * d1 * d2 * d3 * 4;
        bdesc.GuaranteedBaseOffsetAlignment = 0;
        desc.Type = DML_TENSOR_TYPE_BUFFER;
        desc.Desc = &bdesc;
    }

    void set_strided(UINT d0, UINT d1, UINT d2, UINT d3, UINT s0, UINT s1, UINT s2, UINT s3, UINT64 total_bytes) {
        sizes[0] = d0; sizes[1] = d1; sizes[2] = d2; sizes[3] = d3;
        strides[0] = s0; strides[1] = s1; strides[2] = s2; strides[3] = s3;
        bdesc.DataType = DML_TENSOR_DATA_TYPE_FLOAT32;
        bdesc.Flags = DML_TENSOR_FLAG_NONE;
        bdesc.DimensionCount = 4;
        bdesc.Sizes = sizes;
        bdesc.Strides = strides;
        bdesc.TotalTensorSizeInBytes = total_bytes;
        bdesc.GuaranteedBaseOffsetAlignment = 0;
        desc.Type = DML_TENSOR_TYPE_BUFFER;
        desc.Desc = &bdesc;
    }
};

struct OpBindingSpec {
    ComPtr<IDMLCompiledOperator> compiled;
    ComPtr<IDMLBindingTable> bt;
    DmlBuffer persistent;
    std::vector<DML_BUFFER_BINDING> in_bb;
    std::vector<DML_BINDING_TYPE> in_types;
    std::vector<DML_BUFFER_BINDING> out_bb;
    std::vector<DML_BINDING_TYPE> out_types;
    DML_BINDING_PROPERTIES exec_props{};
};

struct DmlPipeline {
    std::vector<OpBindingSpec> ops;
    ComPtr<ID3D12DescriptorHeap> heap;
    DmlBuffer temp_buf;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;

    void reset() {
        ops.clear();
        heap.Reset();
        temp_buf.res.Reset();
        alloc.Reset();
        list.Reset();
    }
};

struct DmlBackend {
    DmlApi api;
    ComPtr<ID3D12Device> d3d_device;
    ComPtr<ID3D12CommandQueue> cmd_queue;
    ComPtr<ID3D12CommandAllocator> cmd_alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd_list;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    uint64_t fence_val = 0;
    ComPtr<IDMLDevice> dml_device;
    ComPtr<IDMLCommandRecorder> cmd_recorder;
    UINT desc_inc = 0;

    DxgiAdapterInfo selected_adapter{};
    DML_FEATURE_LEVEL max_feature_level = DML_FEATURE_LEVEL_1_0;
    size_t gpu_bytes_allocated = 0;

    DmlBuffer upload_buf;
    DmlBuffer readback_buf;

    DmlBuffer buf_ctx_w1, buf_ctx_b1, buf_ctx_w2, buf_ctx_b2;
    DmlBuffer buf_conv0_w, buf_conv0_b, buf_tcn_w2, buf_tcn_b2, buf_tcn_w3, buf_tcn_b3;
    DmlBuffer buf_gru_w, buf_gru_r, buf_gru_b;

    DmlBuffer buf_anchor, buf_theta, buf_grads, buf_m, buf_v;
    DmlBuffer buf_theta_tmp, buf_m_tmp, buf_v_tmp;

    DmlBuffer buf_xseq, buf_xctx, buf_target;
    DmlBuffer buf_ctx1_p, buf_ctx1_s, buf_ctx1, buf_hctx_p, buf_hctx_s, buf_hctx;
    DmlBuffer buf_xseq_nchw, buf_c1_p, buf_c1_s, buf_c1, buf_c2_p, buf_c2_s, buf_c2_act, buf_c2, buf_c3_p, buf_c3_s, buf_c3_act, buf_c3;
    DmlBuffer buf_c1_tb, buf_hseq;
    DmlBuffer buf_z0, buf_pre1, buf_s1, buf_a1, buf_pre2, buf_s2, buf_a2, buf_pre3, buf_s3, buf_a3, buf_raw;
    DmlBuffer buf_raw012, buf_raw34, buf_raw56, buf_raw7;
    DmlBuffer buf_out012, buf_out34, buf_out56_sp, buf_out56, buf_out7, buf_pred;

    DmlBuffer buf_diff, buf_mse_sum, buf_dp;
    DmlBuffer buf_p012, buf_p34, buf_p56, buf_p7;
    DmlBuffer buf_dp012, buf_dp34, buf_dp56, buf_dp7, buf_r56;
    DmlBuffer buf_t012, buf_t34, buf_t56, buf_t7;
    DmlBuffer buf_dr012, buf_dr34, buf_dr56, buf_dr7, buf_draw;
    DmlBuffer buf_da3, buf_sig3, buf_om_s3, buf_p3_s3, buf_tmp3, buf_dpre3;
    DmlBuffer buf_da2, buf_sig2, buf_om_s2, buf_p2_s2, buf_tmp2, buf_dpre2;
    DmlBuffer buf_da1, buf_sig1, buf_om_s1, buf_p1_s1, buf_tmp1, buf_dpre1;
    DmlBuffer buf_diff_th, buf_lam_diff, buf_l2_sq, buf_norm_sq;
    DmlBuffer buf_opt_scalars;
    DmlBuffer buf_gclip, buf_m1, buf_m2, buf_gsq, buf_v1, buf_v2, buf_mhat, buf_vhat, buf_vsqrt, buf_denom, buf_step_div, buf_step, buf_decay_th;

    DmlPipeline pipe_fwd;
    DmlPipeline pipe_bwd;
    DmlPipeline pipe_adamw;

    uint32_t active_batch = 0;
    strafenet::ArchType active_arch = strafenet::ArchType::TCN_ResMLP;
    uint64_t opt_step = 0;
    uint64_t skipped_nonfinite = 0;

    bool create_buffer(uint64_t bytes, D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES init_state, DmlBuffer& out) {
        uint64_t alloc_size = std::max<uint64_t>(256, align_up(bytes, 256));
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = heap_type;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = alloc_size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = flags;
        if (FAILED(d3d_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, init_state, nullptr, IID_PPV_ARGS(&out.res)))) {
            return false;
        }
        out.size = alloc_size;
        if (heap_type == D3D12_HEAP_TYPE_DEFAULT) gpu_bytes_allocated += alloc_size;
        return true;
    }

    bool make_uav(uint64_t bytes, DmlBuffer& b) {
        return create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, b);
    }

    void wait_gpu() {
        fence_val++;
        cmd_queue->Signal(fence.Get(), fence_val);
        if (fence->GetCompletedValue() < fence_val) {
            fence->SetEventOnCompletion(fence_val, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }
    }

    bool init(int preferred_adapter = -1) {
        if (!api.load()) return false;
        auto adapters = enumerate_dxgi_adapters(api);
        if (adapters.empty()) return false;
        int chosen = -1;
        if (preferred_adapter >= 0 && preferred_adapter < static_cast<int>(adapters.size())) {
            chosen = preferred_adapter;
        } else {
            size_t best_vram = 0;
            for (size_t i = 0; i < adapters.size(); ++i) {
                if (!adapters[i].is_software && adapters[i].dedicated_vram >= best_vram) {
                    best_vram = adapters[i].dedicated_vram;
                    chosen = static_cast<int>(i);
                }
            }
            if (chosen < 0) chosen = 0;
        }
        selected_adapter = adapters[chosen];

        ComPtr<IDXGIFactory1> factory;
        if (FAILED(api.fn_CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(static_cast<UINT>(chosen), &adapter))) return false;
        if (FAILED(api.fn_D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d_device)))) return false;

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(d3d_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&cmd_queue)))) return false;
        if (FAILED(d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc)))) return false;
        if (FAILED(d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), nullptr, IID_PPV_ARGS(&cmd_list)))) return false;
        cmd_list->Close();
        if (FAILED(d3d_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event) return false;
        if (FAILED(api.fn_DMLCreateDevice(d3d_device.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml_device)))) return false;

        const DML_FEATURE_LEVEL levels[] = {
            DML_FEATURE_LEVEL_1_0, DML_FEATURE_LEVEL_2_0, DML_FEATURE_LEVEL_2_1,
            DML_FEATURE_LEVEL_3_0, DML_FEATURE_LEVEL_4_0, DML_FEATURE_LEVEL_5_0, DML_FEATURE_LEVEL_6_0
        };
        DML_FEATURE_QUERY_FEATURE_LEVELS fq{ _countof(levels), levels };
        DML_FEATURE_DATA_FEATURE_LEVELS fd{};
        if (SUCCEEDED(dml_device->CheckFeatureSupport(DML_FEATURE_FEATURE_LEVELS, sizeof(fq), &fq, sizeof(fd), &fd))) {
            max_feature_level = fd.MaxSupportedFeatureLevel;
        }
        if (FAILED(dml_device->CreateCommandRecorder(IID_PPV_ARGS(&cmd_recorder)))) return false;
        desc_inc = d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    bool upload_buffer(const DmlBuffer& dst, const void* src, uint64_t bytes, uint64_t dst_offset = 0) {
        if (upload_buf.size < bytes) {
            upload_buf.res.Reset();
            if (!create_buffer(std::max<uint64_t>(bytes, 1024 * 1024), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, upload_buf)) {
                return false;
            }
        }
        void* mapped = nullptr;
        D3D12_RANGE rr{ 0, 0 };
        if (FAILED(upload_buf.res->Map(0, &rr, &mapped))) return false;
        std::memcpy(mapped, src, bytes);
        upload_buf.res->Unmap(0, nullptr);

        cmd_alloc->Reset();
        cmd_list->Reset(cmd_alloc.Get(), nullptr);
        D3D12_RESOURCE_BARRIER b1{};
        b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b1.Transition.pResource = dst.res.Get();
        b1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b1.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b1.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmd_list->ResourceBarrier(1, &b1);
        cmd_list->CopyBufferRegion(dst.res.Get(), dst_offset, upload_buf.res.Get(), 0, bytes);
        D3D12_RESOURCE_BARRIER b2 = b1;
        b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b2.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmd_list->ResourceBarrier(1, &b2);
        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();
        return true;
    }

    bool download_buffer(const DmlBuffer& src, void* dst, uint64_t bytes, uint64_t src_offset = 0) {
        if (readback_buf.size < bytes) {
            readback_buf.res.Reset();
            if (!create_buffer(std::max<uint64_t>(bytes, 512 * 1024), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, readback_buf)) {
                return false;
            }
        }
        cmd_alloc->Reset();
        cmd_list->Reset(cmd_alloc.Get(), nullptr);
        D3D12_RESOURCE_BARRIER b1{};
        b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b1.Transition.pResource = src.res.Get();
        b1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b1.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b1.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmd_list->ResourceBarrier(1, &b1);
        cmd_list->CopyBufferRegion(readback_buf.res.Get(), 0, src.res.Get(), src_offset, bytes);
        D3D12_RESOURCE_BARRIER b2 = b1;
        b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b2.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmd_list->ResourceBarrier(1, &b2);
        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();

        void* mapped = nullptr;
        D3D12_RANGE rr{ 0, static_cast<SIZE_T>(bytes) };
        if (FAILED(readback_buf.res->Map(0, &rr, &mapped))) return false;
        std::memcpy(dst, mapped, bytes);
        D3D12_RANGE wr{ 0, 0 };
        readback_buf.res->Unmap(0, &wr);
        return true;
    }

    bool zero_buffer(const DmlBuffer& dst, uint64_t bytes) {
        std::vector<uint8_t> zeros(bytes, 0);
        return upload_buffer(dst, zeros.data(), bytes, 0);
    }

    bool add_op(
        DmlPipeline& pipe,
        const DML_OPERATOR_DESC& desc,
        std::vector<DML_BUFFER_BINDING> ins,
        std::vector<DML_BUFFER_BINDING> outs,
        std::vector<DML_BINDING_TYPE> in_types = {},
        std::vector<DML_BINDING_TYPE> out_types = {}
    ) {
        ComPtr<IDMLOperator> op;
        HRESULT hr = dml_device->CreateOperator(&desc, IID_PPV_ARGS(&op));
        if (FAILED(hr)) {
            return false;
        }
        OpBindingSpec spec{};
        hr = dml_device->CompileOperator(op.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&spec.compiled));
        if (FAILED(hr)) {
            return false;
        }
        spec.in_bb = std::move(ins);
        spec.out_bb = std::move(outs);
        spec.in_types = in_types.empty() ? std::vector<DML_BINDING_TYPE>(spec.in_bb.size(), DML_BINDING_TYPE_BUFFER) : std::move(in_types);
        spec.out_types = out_types.empty() ? std::vector<DML_BINDING_TYPE>(spec.out_bb.size(), DML_BINDING_TYPE_BUFFER) : std::move(out_types);
        pipe.ops.push_back(std::move(spec));
        return true;
    }

    bool finalize_pipeline(DmlPipeline& pipe, bool record_list = true) {
        if (pipe.ops.empty()) return false;
        std::vector<IDMLCompiledOperator*> raw_ops(pipe.ops.size());
        for (size_t i = 0; i < pipe.ops.size(); ++i) raw_ops[i] = pipe.ops[i].compiled.Get();

        ComPtr<IDMLOperatorInitializer> initializer;
        HRESULT hr = dml_device->CreateOperatorInitializer(static_cast<UINT>(raw_ops.size()), raw_ops.data(), IID_PPV_ARGS(&initializer));
        if (FAILED(hr)) {
            return false;
        }
        DML_BINDING_PROPERTIES init_props = initializer->GetBindingProperties();
        UINT total_descs = std::max<UINT>(init_props.RequiredDescriptorCount, 1);
        uint64_t max_temp = init_props.TemporaryResourceSize;
        std::vector<UINT> offsets(pipe.ops.size());
        for (size_t i = 0; i < pipe.ops.size(); ++i) {
            pipe.ops[i].exec_props = pipe.ops[i].compiled->GetBindingProperties();
            offsets[i] = total_descs;
            total_descs += std::max<UINT>(pipe.ops[i].exec_props.RequiredDescriptorCount, 1);
            max_temp = std::max(max_temp, pipe.ops[i].exec_props.TemporaryResourceSize);
            if (pipe.ops[i].exec_props.PersistentResourceSize > 0) {
                if (!make_uav(pipe.ops[i].exec_props.PersistentResourceSize, pipe.ops[i].persistent)) {
                    return false;
                }
            }
        }
        if (max_temp > 0) {
            if (!make_uav(max_temp, pipe.temp_buf)) {
                return false;
            }
        }

        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors = total_descs;
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(hr = d3d_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&pipe.heap)))) {
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cbase = pipe.heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gbase = pipe.heap->GetGPUDescriptorHandleForHeapStart();

        DML_BINDING_TABLE_DESC init_btd{ initializer.Get(), cbase, gbase, std::max<UINT>(init_props.RequiredDescriptorCount, 1) };
        ComPtr<IDMLBindingTable> init_bt;
        if (FAILED(hr = dml_device->CreateBindingTable(&init_btd, IID_PPV_ARGS(&init_bt)))) {
            return false;
        }
        if (init_props.TemporaryResourceSize > 0) {
            DML_BUFFER_BINDING tb{ pipe.temp_buf.res.Get(), 0, init_props.TemporaryResourceSize };
            DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
            init_bt->BindTemporaryResource(&td);
        }
        std::vector<DML_BUFFER_BINDING> ipbb(pipe.ops.size());
        std::vector<DML_BINDING_DESC> ipbd(pipe.ops.size());
        for (size_t i = 0; i < pipe.ops.size(); ++i) {
            if (pipe.ops[i].exec_props.PersistentResourceSize > 0) {
                ipbb[i] = { pipe.ops[i].persistent.res.Get(), 0, pipe.ops[i].exec_props.PersistentResourceSize };
                ipbd[i] = { DML_BINDING_TYPE_BUFFER, &ipbb[i] };
            } else {
                ipbd[i] = { DML_BINDING_TYPE_NONE, nullptr };
            }
        }
        init_bt->BindOutputs(static_cast<UINT>(ipbd.size()), ipbd.data());

        cmd_alloc->Reset();
        cmd_list->Reset(cmd_alloc.Get(), nullptr);
        ID3D12DescriptorHeap* heaps[] = { pipe.heap.Get() };
        cmd_list->SetDescriptorHeaps(1, heaps);
        cmd_recorder->RecordDispatch(cmd_list.Get(), initializer.Get(), init_bt.Get());
        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();
        if (FAILED(hr = d3d_device->GetDeviceRemovedReason())) {
            return false;
        }

        for (size_t i = 0; i < pipe.ops.size(); ++i) {
            auto& op = pipe.ops[i];
            DML_BINDING_TABLE_DESC btd{};
            btd.Dispatchable = op.compiled.Get();
            btd.CPUDescriptorHandle.ptr = cbase.ptr + static_cast<SIZE_T>(offsets[i]) * desc_inc;
            btd.GPUDescriptorHandle.ptr = gbase.ptr + static_cast<UINT64>(offsets[i]) * desc_inc;
            btd.SizeInDescriptors = std::max<UINT>(op.exec_props.RequiredDescriptorCount, 1);
            if (FAILED(hr = dml_device->CreateBindingTable(&btd, IID_PPV_ARGS(&op.bt)))) {
                return false;
            }

            std::vector<DML_BINDING_DESC> idesc(op.in_bb.size());
            for (size_t k = 0; k < op.in_bb.size(); ++k) {
                idesc[k] = { op.in_types[k], op.in_types[k] == DML_BINDING_TYPE_NONE ? nullptr : &op.in_bb[k] };
            }
            op.bt->BindInputs(static_cast<UINT>(idesc.size()), idesc.data());

            std::vector<DML_BINDING_DESC> odesc(op.out_bb.size());
            for (size_t k = 0; k < op.out_bb.size(); ++k) {
                odesc[k] = { op.out_types[k], op.out_types[k] == DML_BINDING_TYPE_NONE ? nullptr : &op.out_bb[k] };
            }
            op.bt->BindOutputs(static_cast<UINT>(odesc.size()), odesc.data());

            if (op.exec_props.TemporaryResourceSize > 0) {
                DML_BUFFER_BINDING tb{ pipe.temp_buf.res.Get(), 0, op.exec_props.TemporaryResourceSize };
                DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
                op.bt->BindTemporaryResource(&td);
            }
            if (op.exec_props.PersistentResourceSize > 0) {
                DML_BUFFER_BINDING pb{ op.persistent.res.Get(), 0, op.exec_props.PersistentResourceSize };
                DML_BINDING_DESC pd{ DML_BINDING_TYPE_BUFFER, &pb };
                op.bt->BindPersistentResource(&pd);
            }
        }

        if (record_list) {
            if (FAILED(d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pipe.alloc)))) return false;
            if (FAILED(d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pipe.alloc.Get(), nullptr, IID_PPV_ARGS(&pipe.list)))) return false;
            pipe.list->SetDescriptorHeaps(1, heaps);
            D3D12_RESOURCE_BARRIER uav{};
            uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            for (size_t i = 0; i < pipe.ops.size(); ++i) {
                cmd_recorder->RecordDispatch(pipe.list.Get(), pipe.ops[i].compiled.Get(), pipe.ops[i].bt.Get());
                if (i + 1 < pipe.ops.size()) pipe.list->ResourceBarrier(1, &uav);
            }
            pipe.list->Close();
        }
        return true;
    }

    void run_pipeline(const DmlPipeline& pipe) {
        ID3D12CommandList* lists[] = { pipe.list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();
    }

    void release_model_buffers() {
        pipe_fwd.reset();
        pipe_bwd.reset();
        pipe_adamw.reset();
        auto r = [](DmlBuffer& b) { b.res.Reset(); b.size = 0; };
        r(buf_ctx_w1); r(buf_ctx_b1); r(buf_ctx_w2); r(buf_ctx_b2);
        r(buf_conv0_w); r(buf_conv0_b); r(buf_tcn_w2); r(buf_tcn_b2); r(buf_tcn_w3); r(buf_tcn_b3);
        r(buf_gru_w); r(buf_gru_r); r(buf_gru_b);
        r(buf_anchor); r(buf_theta); r(buf_grads); r(buf_m); r(buf_v);
        r(buf_theta_tmp); r(buf_m_tmp); r(buf_v_tmp);
        r(buf_xseq); r(buf_xctx); r(buf_target);
        r(buf_ctx1_p); r(buf_ctx1_s); r(buf_ctx1); r(buf_hctx_p); r(buf_hctx_s); r(buf_hctx);
        r(buf_xseq_nchw); r(buf_c1_p); r(buf_c1_s); r(buf_c1); r(buf_c2_p); r(buf_c2_s); r(buf_c2_act); r(buf_c2); r(buf_c3_p); r(buf_c3_s); r(buf_c3_act); r(buf_c3);
        r(buf_c1_tb); r(buf_hseq);
        r(buf_z0); r(buf_pre1); r(buf_s1); r(buf_a1); r(buf_pre2); r(buf_s2); r(buf_a2); r(buf_pre3); r(buf_s3); r(buf_a3); r(buf_raw);
        r(buf_raw012); r(buf_raw34); r(buf_raw56); r(buf_raw7);
        r(buf_out012); r(buf_out34); r(buf_out56_sp); r(buf_out56); r(buf_out7); r(buf_pred);
        r(buf_diff); r(buf_mse_sum); r(buf_dp);
        r(buf_p012); r(buf_p34); r(buf_p56); r(buf_p7);
        r(buf_dp012); r(buf_dp34); r(buf_dp56); r(buf_dp7); r(buf_r56);
        r(buf_t012); r(buf_t34); r(buf_t56); r(buf_t7);
        r(buf_dr012); r(buf_dr34); r(buf_dr56); r(buf_dr7); r(buf_draw);
        r(buf_da3); r(buf_sig3); r(buf_om_s3); r(buf_p3_s3); r(buf_tmp3); r(buf_dpre3);
        r(buf_da2); r(buf_sig2); r(buf_om_s2); r(buf_p2_s2); r(buf_tmp2); r(buf_dpre2);
        r(buf_da1); r(buf_sig1); r(buf_om_s1); r(buf_p1_s1); r(buf_tmp1); r(buf_dpre1);
        r(buf_diff_th); r(buf_lam_diff); r(buf_l2_sq); r(buf_norm_sq);
        r(buf_opt_scalars);
        r(buf_gclip); r(buf_m1); r(buf_m2); r(buf_gsq); r(buf_v1); r(buf_v2); r(buf_mhat); r(buf_vhat); r(buf_vsqrt); r(buf_denom); r(buf_step_div); r(buf_step); r(buf_decay_th);
        gpu_bytes_allocated = 0;
    }

    static std::vector<float> convert_conv1d_filter(const float* w_rowmajor, uint32_t c_in, uint32_t c_out) {
        std::vector<float> f(static_cast<size_t>(c_out) * c_in * 3);
        for (uint32_t co = 0; co < c_out; ++co) {
            for (uint32_t ci = 0; ci < c_in; ++ci) {
                for (uint32_t tap = 0; tap < 3; ++tap) {
                    f[(static_cast<size_t>(co) * c_in + ci) * 3 + tap] = w_rowmajor[(tap * c_in + ci) * c_out + co];
                }
            }
        }
        return f;
    }

    bool build_pipelines(const strafenet::StrafeNetWeights& w, uint32_t batch, const strafenet::TrainConfig& cfg) {
        using D = strafenet::ModelDims;
        release_model_buffers();
        active_batch = batch;
        active_arch = w.arch;
        opt_step = 0;
        skipped_nonfinite = 0;

        if (!make_uav(D::CTX_W1 * 4, buf_ctx_w1) || !make_uav(D::CTX_B1 * 4, buf_ctx_b1) ||
            !make_uav(D::CTX_W2 * 4, buf_ctx_w2) || !make_uav(D::CTX_B2 * 4, buf_ctx_b2) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_anchor) || !make_uav(D::PLASTIC_TOTAL * 4, buf_theta) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_grads) || !make_uav(D::PLASTIC_TOTAL * 4, buf_m) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_v) || !make_uav(D::PLASTIC_TOTAL * 4, buf_theta_tmp) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_m_tmp) || !make_uav(D::PLASTIC_TOTAL * 4, buf_v_tmp)) {
            return false;
        }

        const float* bb = w.backbone.data();
        if (!upload_buffer(buf_ctx_w1, bb + w.off_ctx_w1, D::CTX_W1 * 4) ||
            !upload_buffer(buf_ctx_b1, bb + w.off_ctx_b1, D::CTX_B1 * 4) ||
            !upload_buffer(buf_ctx_w2, bb + w.off_ctx_w2, D::CTX_W2 * 4) ||
            !upload_buffer(buf_ctx_b2, bb + w.off_ctx_b2, D::CTX_B2 * 4) ||
            !upload_buffer(buf_anchor, w.anchor_theta0.data(), D::PLASTIC_TOTAL * 4) ||
            !upload_buffer(buf_theta, w.plastic_theta.data(), D::PLASTIC_TOTAL * 4) ||
            !zero_buffer(buf_m, D::PLASTIC_TOTAL * 4) ||
            !zero_buffer(buf_v, D::PLASTIC_TOTAL * 4)) {
            return false;
        }

        if (w.arch == strafenet::ArchType::TCN_ResMLP) {
            if (!make_uav(D::TCN_W1 * 4, buf_conv0_w) || !make_uav(D::TCN_B1 * 4, buf_conv0_b) ||
                !make_uav(D::TCN_W2 * 4, buf_tcn_w2) || !make_uav(D::TCN_B2 * 4, buf_tcn_b2) ||
                !make_uav(D::TCN_W3 * 4, buf_tcn_w3) || !make_uav(D::TCN_B3 * 4, buf_tcn_b3)) {
                return false;
            }
            auto f1 = convert_conv1d_filter(bb + w.off_tcn_w1, D::D_SEQ, D::HIDDEN);
            auto f2 = convert_conv1d_filter(bb + w.off_tcn_w2, D::HIDDEN, D::HIDDEN);
            auto f3 = convert_conv1d_filter(bb + w.off_tcn_w3, D::HIDDEN, D::HIDDEN);
            if (!upload_buffer(buf_conv0_w, f1.data(), f1.size() * 4) ||
                !upload_buffer(buf_conv0_b, bb + w.off_tcn_b1, D::TCN_B1 * 4) ||
                !upload_buffer(buf_tcn_w2, f2.data(), f2.size() * 4) ||
                !upload_buffer(buf_tcn_b2, bb + w.off_tcn_b2, D::TCN_B2 * 4) ||
                !upload_buffer(buf_tcn_w3, f3.data(), f3.size() * 4) ||
                !upload_buffer(buf_tcn_b3, bb + w.off_tcn_b3, D::TCN_B3 * 4)) {
                return false;
            }
        } else {
            if (!make_uav(D::GRU_CONV_W * 4, buf_conv0_w) || !make_uav(D::GRU_CONV_B * 4, buf_conv0_b) ||
                !make_uav(D::GRU_W_IH * 4, buf_gru_w) || !make_uav(D::GRU_W_HH * 4, buf_gru_r) ||
                !make_uav((D::GRU_B_IH + D::GRU_B_HH) * 4, buf_gru_b)) {
                return false;
            }
            auto f1 = convert_conv1d_filter(bb + w.off_gru_conv_w, D::D_SEQ, D::HIDDEN);
            std::vector<float> gru_wt(D::GRU_W_IH);
            for (uint32_t k = 0; k < D::HIDDEN; ++k)
                for (uint32_t j = 0; j < 3 * D::HIDDEN; ++j)
                    gru_wt[j * D::HIDDEN + k] = bb[w.off_gru_w_ih + k * (3 * D::HIDDEN) + j];
            std::vector<float> gru_rt(D::GRU_W_HH);
            for (uint32_t k = 0; k < D::HIDDEN; ++k)
                for (uint32_t j = 0; j < 3 * D::HIDDEN; ++j)
                    gru_rt[j * D::HIDDEN + k] = bb[w.off_gru_w_hh + k * (3 * D::HIDDEN) + j];
            std::vector<float> gru_b(D::GRU_B_IH + D::GRU_B_HH);
            std::memcpy(gru_b.data(), bb + w.off_gru_b_ih, D::GRU_B_IH * 4);
            std::memcpy(gru_b.data() + D::GRU_B_IH, bb + w.off_gru_b_hh, D::GRU_B_HH * 4);
            if (!upload_buffer(buf_conv0_w, f1.data(), f1.size() * 4) ||
                !upload_buffer(buf_conv0_b, bb + w.off_gru_conv_b, D::GRU_CONV_B * 4) ||
                !upload_buffer(buf_gru_w, gru_wt.data(), gru_wt.size() * 4) ||
                !upload_buffer(buf_gru_r, gru_rt.data(), gru_rt.size() * 4) ||
                !upload_buffer(buf_gru_b, gru_b.data(), gru_b.size() * 4)) {
                return false;
            }
        }

        if (!make_uav(batch * D::SEQ_LEN * D::D_SEQ * 4, buf_xseq) ||
            !make_uav(batch * D::D_CTX * 4, buf_xctx) ||
            !make_uav(batch * D::OUT_TOTAL * 4, buf_target) ||
            !make_uav(batch * D::HIDDEN * 4, buf_ctx1_p) || !make_uav(batch * D::HIDDEN * 4, buf_ctx1_s) || !make_uav(batch * D::HIDDEN * 4, buf_ctx1) ||
            !make_uav(batch * D::HIDDEN * 4, buf_hctx_p) || !make_uav(batch * D::HIDDEN * 4, buf_hctx_s) || !make_uav(batch * D::HIDDEN * 4, buf_hctx) ||
            !make_uav(batch * D::SEQ_LEN * D::D_SEQ * 4, buf_xseq_nchw) ||
            !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c1_p) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c1_s) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c1) ||
            !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c2_p) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c2_s) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c2_act) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c2) ||
            !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c3_p) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c3_s) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c3_act) || !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c3) ||
            !make_uav(batch * D::SEQ_LEN * D::HIDDEN * 4, buf_c1_tb) ||
            !make_uav(batch * D::HIDDEN * 4, buf_hseq) ||
            !make_uav(batch * D::TRUNK_H * 4, buf_z0) ||
            !make_uav(batch * D::TRUNK_H * 4, buf_pre1) || !make_uav(batch * D::TRUNK_H * 4, buf_s1) || !make_uav(batch * D::TRUNK_H * 4, buf_a1) ||
            !make_uav(batch * D::HIDDEN * 4, buf_pre2) || !make_uav(batch * D::HIDDEN * 4, buf_s2) || !make_uav(batch * D::HIDDEN * 4, buf_a2) ||
            !make_uav(batch * D::HIDDEN * 4, buf_pre3) || !make_uav(batch * D::HIDDEN * 4, buf_s3) || !make_uav(batch * D::HIDDEN * 4, buf_a3) ||
            !make_uav(batch * D::OUT_TOTAL * 4, buf_raw) ||
            !make_uav(batch * D::OUT_STEPS * 3 * 4, buf_raw012) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_raw34) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_raw56) ||
            !make_uav(batch * D::OUT_STEPS * 1 * 4, buf_raw7) ||
            !make_uav(batch * D::OUT_STEPS * 3 * 4, buf_out012) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_out34) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_out56_sp) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_out56) ||
            !make_uav(batch * D::OUT_STEPS * 1 * 4, buf_out7) ||
            !make_uav(batch * D::OUT_TOTAL * 4, buf_pred)) {
            return false;
        }

        if (!make_uav(batch * D::OUT_TOTAL * 4, buf_diff) || !make_uav(256, buf_mse_sum) || !make_uav(batch * D::OUT_TOTAL * 4, buf_dp) ||
            !make_uav(batch * D::OUT_STEPS * 3 * 4, buf_p012) || !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_p34) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_p56) || !make_uav(batch * D::OUT_STEPS * 1 * 4, buf_p7) ||
            !make_uav(batch * D::OUT_STEPS * 3 * 4, buf_dp012) || !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_dp34) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_dp56) || !make_uav(batch * D::OUT_STEPS * 1 * 4, buf_dp7) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_r56) ||
            !make_uav(batch * D::OUT_STEPS * 3 * 4, buf_t012) || !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_t34) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_t56) || !make_uav(batch * D::OUT_STEPS * 1 * 4, buf_t7) ||
            !make_uav(batch * D::OUT_STEPS * 3 * 4, buf_dr012) || !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_dr34) ||
            !make_uav(batch * D::OUT_STEPS * 2 * 4, buf_dr56) || !make_uav(batch * D::OUT_STEPS * 1 * 4, buf_dr7) ||
            !make_uav(batch * D::OUT_TOTAL * 4, buf_draw) ||
            !make_uav(batch * D::HIDDEN * 4, buf_da3) || !make_uav(batch * D::HIDDEN * 4, buf_sig3) ||
            !make_uav(batch * D::HIDDEN * 4, buf_om_s3) || !make_uav(batch * D::HIDDEN * 4, buf_p3_s3) ||
            !make_uav(batch * D::HIDDEN * 4, buf_tmp3) || !make_uav(batch * D::HIDDEN * 4, buf_dpre3) ||
            !make_uav(batch * D::HIDDEN * 4, buf_da2) || !make_uav(batch * D::HIDDEN * 4, buf_sig2) ||
            !make_uav(batch * D::HIDDEN * 4, buf_om_s2) || !make_uav(batch * D::HIDDEN * 4, buf_p2_s2) ||
            !make_uav(batch * D::HIDDEN * 4, buf_tmp2) || !make_uav(batch * D::HIDDEN * 4, buf_dpre2) ||
            !make_uav(batch * D::TRUNK_H * 4, buf_da1) || !make_uav(batch * D::TRUNK_H * 4, buf_sig1) ||
            !make_uav(batch * D::TRUNK_H * 4, buf_om_s1) || !make_uav(batch * D::TRUNK_H * 4, buf_p1_s1) ||
            !make_uav(batch * D::TRUNK_H * 4, buf_tmp1) || !make_uav(batch * D::TRUNK_H * 4, buf_dpre1) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_diff_th) || !make_uav(D::PLASTIC_TOTAL * 4, buf_lam_diff) ||
            !make_uav(256, buf_l2_sq) || !make_uav(256, buf_norm_sq) || !make_uav(256, buf_opt_scalars) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_gclip) || !make_uav(D::PLASTIC_TOTAL * 4, buf_m1) || !make_uav(D::PLASTIC_TOTAL * 4, buf_m2) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_gsq) || !make_uav(D::PLASTIC_TOTAL * 4, buf_v1) || !make_uav(D::PLASTIC_TOTAL * 4, buf_v2) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_mhat) || !make_uav(D::PLASTIC_TOTAL * 4, buf_vhat) || !make_uav(D::PLASTIC_TOTAL * 4, buf_vsqrt) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_denom) || !make_uav(D::PLASTIC_TOTAL * 4, buf_step_div) || !make_uav(D::PLASTIC_TOTAL * 4, buf_step) ||
            !make_uav(D::PLASTIC_TOTAL * 4, buf_decay_th)) {
            return false;
        }

        auto bb_bind = [](const DmlBuffer& b, uint64_t bytes) -> DML_BUFFER_BINDING { return { b.res.Get(), 0, bytes }; };
        auto pt_bind = [&](const DmlBuffer& b, size_t off_floats, uint64_t floats) -> DML_BUFFER_BINDING {
            return { b.res.Get(), off_floats * 4, floats * 4 };
        };

        auto add_silu_seq = [&](DmlPipeline& pipe, const DmlBuffer& pre, const DmlBuffer& sig, const DmlBuffer& out, UINT d0, UINT d1, UINT d2, UINT d3) -> bool {
            Tensor4D t; t.set(d0, d1, d2, d3);
            UINT64 bytes = static_cast<UINT64>(d0) * d1 * d2 * d3 * 4;
            DML_ACTIVATION_SIGMOID_OPERATOR_DESC sd{ &t.desc, &t.desc };
            DML_OPERATOR_DESC sod{ DML_OPERATOR_ACTIVATION_SIGMOID, &sd };
            if (!add_op(pipe, sod, { bb_bind(pre, bytes) }, { bb_bind(sig, bytes) })) return false;
            DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC md{ &t.desc, &t.desc, &t.desc };
            DML_OPERATOR_DESC mod{ DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &md };
            return add_op(pipe, mod, { bb_bind(pre, bytes), bb_bind(sig, bytes) }, { bb_bind(out, bytes) });
        };

        {
            Tensor4D tx, tw1, tb1, tc1, tw2, tb2, thctx;
            tx.set(1, 1, batch, D::D_CTX);
            tw1.set(1, 1, D::D_CTX, D::HIDDEN);
            tb1.set_strided(1, 1, batch, D::HIDDEN, 0, 0, 0, 1, D::HIDDEN * 4);
            tc1.set(1, 1, batch, D::HIDDEN);
            tw2.set(1, 1, D::HIDDEN, D::HIDDEN);
            tb2.set_strided(1, 1, batch, D::HIDDEN, 0, 0, 0, 1, D::HIDDEN * 4);
            thctx.set(1, 1, batch, D::HIDDEN);

            DML_GEMM_OPERATOR_DESC g1{ &tx.desc, &tw1.desc, &tb1.desc, &tc1.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_GEMM, &g1 }, { bb_bind(buf_xctx, batch * D::D_CTX * 4), bb_bind(buf_ctx_w1, D::CTX_W1 * 4), bb_bind(buf_ctx_b1, D::CTX_B1 * 4) }, { bb_bind(buf_ctx1_p, batch * D::HIDDEN * 4) })) return false;
            if (!add_silu_seq(pipe_fwd, buf_ctx1_p, buf_ctx1_s, buf_ctx1, 1, 1, batch, D::HIDDEN)) return false;

            DML_GEMM_OPERATOR_DESC g2{ &tc1.desc, &tw2.desc, &tb2.desc, &thctx.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_GEMM, &g2 }, { bb_bind(buf_ctx1, batch * D::HIDDEN * 4), bb_bind(buf_ctx_w2, D::CTX_W2 * 4), bb_bind(buf_ctx_b2, D::CTX_B2 * 4) }, { bb_bind(buf_hctx_p, batch * D::HIDDEN * 4) })) return false;
            if (!add_silu_seq(pipe_fwd, buf_hctx_p, buf_hctx_s, buf_hctx, 1, 1, batch, D::HIDDEN)) return false;

            Tensor4D tin_strided, tnchw0;
            tin_strided.set_strided(batch, D::D_SEQ, 1, D::SEQ_LEN, D::SEQ_LEN * D::D_SEQ, 1, D::D_SEQ, D::D_SEQ, batch * D::SEQ_LEN * D::D_SEQ * 4);
            tnchw0.set(batch, D::D_SEQ, 1, D::SEQ_LEN);
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id0{ &tin_strided.desc, &tnchw0.desc, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id0 }, { bb_bind(buf_xseq, batch * D::SEQ_LEN * D::D_SEQ * 4) }, { bb_bind(buf_xseq_nchw, batch * D::SEQ_LEN * D::D_SEQ * 4) })) return false;

            auto add_causal_conv = [&](const DmlBuffer& in_b, const DmlBuffer& w_b, const DmlBuffer& b_b, const DmlBuffer& out_p, const DmlBuffer& out_s, const DmlBuffer& out_a, UINT c_in, UINT dil) -> bool {
                Tensor4D ci, cf, cb, co;
                ci.set(batch, c_in, 1, D::SEQ_LEN);
                cf.set(D::HIDDEN, c_in, 1, 3);
                cb.set(1, D::HIDDEN, 1, 1);
                co.set(batch, D::HIDDEN, 1, D::SEQ_LEN);
                UINT strides[2] = { 1, 1 }, dils[2] = { 1, dil }, spad[2] = { 0, 2 * dil }, epad[2] = { 0, 0 }, opad[2] = { 0, 0 };
                DML_CONVOLUTION_OPERATOR_DESC cd{
                    &ci.desc, &cf.desc, &cb.desc, &co.desc,
                    DML_CONVOLUTION_MODE_CROSS_CORRELATION, DML_CONVOLUTION_DIRECTION_FORWARD,
                    2, strides, dils, spad, epad, opad, 1, nullptr
                };
                if (!add_op(pipe_fwd, { DML_OPERATOR_CONVOLUTION, &cd }, { bb_bind(in_b, batch * c_in * D::SEQ_LEN * 4), bb_bind(w_b, c_in * 3 * D::HIDDEN * 4), bb_bind(b_b, D::HIDDEN * 4) }, { bb_bind(out_p, batch * D::HIDDEN * D::SEQ_LEN * 4) })) return false;
                return add_silu_seq(pipe_fwd, out_p, out_s, out_a, batch, D::HIDDEN, 1, D::SEQ_LEN);
            };

            if (!add_causal_conv(buf_xseq_nchw, buf_conv0_w, buf_conv0_b, buf_c1_p, buf_c1_s, buf_c1, D::D_SEQ, 1)) return false;

            if (w.arch == strafenet::ArchType::TCN_ResMLP) {
                if (!add_causal_conv(buf_c1, buf_tcn_w2, buf_tcn_b2, buf_c2_p, buf_c2_s, buf_c2_act, D::HIDDEN, 2)) return false;
                Tensor4D tc_nchw; tc_nchw.set(batch, D::HIDDEN, 1, D::SEQ_LEN);
                DML_ELEMENT_WISE_ADD_OPERATOR_DESC add_c2{ &tc_nchw.desc, &tc_nchw.desc, &tc_nchw.desc };
                if (!add_op(pipe_fwd, { DML_OPERATOR_ELEMENT_WISE_ADD, &add_c2 }, { bb_bind(buf_c1, batch * D::HIDDEN * D::SEQ_LEN * 4), bb_bind(buf_c2_act, batch * D::HIDDEN * D::SEQ_LEN * 4) }, { bb_bind(buf_c2, batch * D::HIDDEN * D::SEQ_LEN * 4) })) return false;

                if (!add_causal_conv(buf_c2, buf_tcn_w3, buf_tcn_b3, buf_c3_p, buf_c3_s, buf_c3_act, D::HIDDEN, 4)) return false;
                if (!add_op(pipe_fwd, { DML_OPERATOR_ELEMENT_WISE_ADD, &add_c2 }, { bb_bind(buf_c2, batch * D::HIDDEN * D::SEQ_LEN * 4), bb_bind(buf_c3_act, batch * D::HIDDEN * D::SEQ_LEN * 4) }, { bb_bind(buf_c3, batch * D::HIDDEN * D::SEQ_LEN * 4) })) return false;

                Tensor4D tc3, ths;
                tc3.set(batch, D::HIDDEN, 1, D::SEQ_LEN);
                ths.set(batch, D::HIDDEN, 1, 1);
                UINT sl_off[4] = { 0, 0, 0, D::SEQ_LEN - 1 };
                UINT sl_sz[4] = { batch, D::HIDDEN, 1, 1 };
                UINT sl_st[4] = { 1, 1, 1, 1 };
                DML_SLICE_OPERATOR_DESC sd{ &tc3.desc, &ths.desc, 4, sl_off, sl_sz, sl_st };
                if (!add_op(pipe_fwd, { DML_OPERATOR_SLICE, &sd }, { bb_bind(buf_c3, batch * D::HIDDEN * D::SEQ_LEN * 4) }, { bb_bind(buf_hseq, batch * D::HIDDEN * 4) })) return false;
            } else {
                Tensor4D tc1_strided, tc1_tb;
                tc1_strided.set_strided(1, D::SEQ_LEN, batch, D::HIDDEN, D::SEQ_LEN * batch * D::HIDDEN, 1, D::HIDDEN * D::SEQ_LEN, D::SEQ_LEN, batch * D::HIDDEN * D::SEQ_LEN * 4);
                tc1_tb.set(1, D::SEQ_LEN, batch, D::HIDDEN);
                DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_tb{ &tc1_strided.desc, &tc1_tb.desc, nullptr };
                if (!add_op(pipe_fwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_tb }, { bb_bind(buf_c1, batch * D::HIDDEN * D::SEQ_LEN * 4) }, { bb_bind(buf_c1_tb, batch * D::HIDDEN * D::SEQ_LEN * 4) })) return false;

                Tensor4D tgw, tgr, tgb, ths;
                tgw.set(1, 1, 3 * D::HIDDEN, D::HIDDEN);
                tgr.set(1, 1, 3 * D::HIDDEN, D::HIDDEN);
                tgb.set(1, 1, 1, 6 * D::HIDDEN);
                ths.set(1, 1, batch, D::HIDDEN);
                DML_ACTIVATION_SIGMOID_OPERATOR_DESC gsig{ nullptr, nullptr };
                DML_ACTIVATION_TANH_OPERATOR_DESC gtanh{ nullptr, nullptr };
                DML_OPERATOR_DESC gacts[2] = { { DML_OPERATOR_ACTIVATION_SIGMOID, &gsig }, { DML_OPERATOR_ACTIVATION_TANH, &gtanh } };
                DML_GRU_OPERATOR_DESC gd{
                    &tc1_tb.desc, &tgw.desc, &tgr.desc, &tgb.desc,
                    nullptr, nullptr, nullptr, &ths.desc,
                    2, gacts, DML_RECURRENT_NETWORK_DIRECTION_FORWARD, TRUE
                };
                if (!add_op(
                    pipe_fwd, { DML_OPERATOR_GRU, &gd },
                    { bb_bind(buf_c1_tb, batch * D::SEQ_LEN * D::HIDDEN * 4), bb_bind(buf_gru_w, D::GRU_W_IH * 4), bb_bind(buf_gru_r, D::GRU_W_HH * 4), bb_bind(buf_gru_b, (D::GRU_B_IH + D::GRU_B_HH) * 4), { nullptr, 0, 0 }, { nullptr, 0, 0 } },
                    { { nullptr, 0, 0 }, bb_bind(buf_hseq, batch * D::HIDDEN * 4) },
                    { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE, DML_BINDING_TYPE_NONE },
                    { DML_BINDING_TYPE_NONE, DML_BINDING_TYPE_BUFFER }
                )) return false;
            }

            Tensor4D th1, th2, tz0;
            th1.set(1, 1, batch, D::HIDDEN);
            th2.set(1, 1, batch, D::HIDDEN);
            tz0.set(1, 1, batch, D::TRUNK_H);
            DML_TENSOR_DESC j_ins[2] = { th1.desc, th2.desc };
            DML_JOIN_OPERATOR_DESC jd{ 2, j_ins, &tz0.desc, 3 };
            if (!add_op(pipe_fwd, { DML_OPERATOR_JOIN, &jd }, { bb_bind(buf_hseq, batch * D::HIDDEN * 4), bb_bind(buf_hctx, batch * D::HIDDEN * 4) }, { bb_bind(buf_z0, batch * D::TRUNK_H * 4) })) return false;

            Tensor4D ttrw1, ttrb1, tpre1, ta1, ttrw2, ttrb2, tpre2, ta2, ttrw3, ttrb3, tpre3, ta3, ttrw4, ttrb4, traw;
            ttrw1.set(1, 1, D::TRUNK_H, D::TRUNK_H);
            ttrb1.set_strided(1, 1, batch, D::TRUNK_H, 0, 0, 0, 1, D::TRUNK_H * 4);
            tpre1.set(1, 1, batch, D::TRUNK_H);
            ta1.set(1, 1, batch, D::TRUNK_H);
            ttrw2.set(1, 1, D::TRUNK_H, D::HIDDEN);
            ttrb2.set_strided(1, 1, batch, D::HIDDEN, 0, 0, 0, 1, D::HIDDEN * 4);
            tpre2.set(1, 1, batch, D::HIDDEN);
            ta2.set(1, 1, batch, D::HIDDEN);
            ttrw3.set(1, 1, D::HIDDEN, D::HIDDEN);
            ttrb3.set_strided(1, 1, batch, D::HIDDEN, 0, 0, 0, 1, D::HIDDEN * 4);
            tpre3.set(1, 1, batch, D::HIDDEN);
            ta3.set(1, 1, batch, D::HIDDEN);
            ttrw4.set(1, 1, D::HIDDEN, D::OUT_TOTAL);
            ttrb4.set_strided(1, 1, batch, D::OUT_TOTAL, 0, 0, 0, 1, D::OUT_TOTAL * 4);
            traw.set(1, 1, batch, D::OUT_TOTAL);

            DML_GEMM_OPERATOR_DESC g_t1{ &tz0.desc, &ttrw1.desc, &ttrb1.desc, &tpre1.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_GEMM, &g_t1 }, { bb_bind(buf_z0, batch * D::TRUNK_H * 4), pt_bind(buf_theta, w.off_tr_w1, D::TR_W1), pt_bind(buf_theta, w.off_tr_b1, D::TR_B1) }, { bb_bind(buf_pre1, batch * D::TRUNK_H * 4) })) return false;
            if (!add_silu_seq(pipe_fwd, buf_pre1, buf_s1, buf_a1, 1, 1, batch, D::TRUNK_H)) return false;

            DML_GEMM_OPERATOR_DESC g_t2{ &ta1.desc, &ttrw2.desc, &ttrb2.desc, &tpre2.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_GEMM, &g_t2 }, { bb_bind(buf_a1, batch * D::TRUNK_H * 4), pt_bind(buf_theta, w.off_tr_w2, D::TR_W2), pt_bind(buf_theta, w.off_tr_b2, D::TR_B2) }, { bb_bind(buf_pre2, batch * D::HIDDEN * 4) })) return false;
            if (!add_silu_seq(pipe_fwd, buf_pre2, buf_s2, buf_a2, 1, 1, batch, D::HIDDEN)) return false;

            DML_GEMM_OPERATOR_DESC g_t3{ &ta2.desc, &ttrw3.desc, &ttrb3.desc, &tpre3.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_GEMM, &g_t3 }, { bb_bind(buf_a2, batch * D::HIDDEN * 4), pt_bind(buf_theta, w.off_tr_w3, D::TR_W3), pt_bind(buf_theta, w.off_tr_b3, D::TR_B3) }, { bb_bind(buf_pre3, batch * D::HIDDEN * 4) })) return false;
            if (!add_silu_seq(pipe_fwd, buf_pre3, buf_s3, buf_a3, 1, 1, batch, D::HIDDEN)) return false;

            DML_GEMM_OPERATOR_DESC g_t4{ &ta3.desc, &ttrw4.desc, &ttrb4.desc, &traw.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
            if (!add_op(pipe_fwd, { DML_OPERATOR_GEMM, &g_t4 }, { bb_bind(buf_a3, batch * D::HIDDEN * 4), pt_bind(buf_theta, w.off_tr_w4, D::TR_W4), pt_bind(buf_theta, w.off_tr_b4, D::TR_B4) }, { bb_bind(buf_raw, batch * D::OUT_TOTAL * 4) })) return false;

            Tensor4D traw4d, t012, t34, t56, t7;
            traw4d.set(1, batch, D::OUT_STEPS, 8);
            t012.set(1, batch, D::OUT_STEPS, 3);
            t34.set(1, batch, D::OUT_STEPS, 2);
            t56.set(1, batch, D::OUT_STEPS, 2);
            t7.set(1, batch, D::OUT_STEPS, 1);

            UINT st1[4] = { 1, 1, 1, 1 };
            UINT off0[4] = { 0, 0, 0, 0 }, sz0[4] = { 1, batch, D::OUT_STEPS, 3 };
            UINT off3[4] = { 0, 0, 0, 3 }, sz3[4] = { 1, batch, D::OUT_STEPS, 2 };
            UINT off5[4] = { 0, 0, 0, 5 }, sz5[4] = { 1, batch, D::OUT_STEPS, 2 };
            UINT off7[4] = { 0, 0, 0, 7 }, sz7[4] = { 1, batch, D::OUT_STEPS, 1 };
            DML_SLICE_OPERATOR_DESC sl0{ &traw4d.desc, &t012.desc, 4, off0, sz0, st1 };
            DML_SLICE_OPERATOR_DESC sl3{ &traw4d.desc, &t34.desc, 4, off3, sz3, st1 };
            DML_SLICE_OPERATOR_DESC sl5{ &traw4d.desc, &t56.desc, 4, off5, sz5, st1 };
            DML_SLICE_OPERATOR_DESC sl7{ &traw4d.desc, &t7.desc, 4, off7, sz7, st1 };
            if (!add_op(pipe_fwd, { DML_OPERATOR_SLICE, &sl0 }, { bb_bind(buf_raw, batch * D::OUT_TOTAL * 4) }, { bb_bind(buf_raw012, batch * D::OUT_STEPS * 3 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_SLICE, &sl3 }, { bb_bind(buf_raw, batch * D::OUT_TOTAL * 4) }, { bb_bind(buf_raw34, batch * D::OUT_STEPS * 2 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_SLICE, &sl5 }, { bb_bind(buf_raw, batch * D::OUT_TOTAL * 4) }, { bb_bind(buf_raw56, batch * D::OUT_STEPS * 2 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_SLICE, &sl7 }, { bb_bind(buf_raw, batch * D::OUT_TOTAL * 4) }, { bb_bind(buf_raw7, batch * D::OUT_STEPS * 1 * 4) })) return false;

            DML_ACTIVATION_TANH_OPERATOR_DESC td0{ &t012.desc, &t012.desc };
            DML_ACTIVATION_SIGMOID_OPERATOR_DESC sd3{ &t34.desc, &t34.desc };
            DML_ACTIVATION_SOFTPLUS_OPERATOR_DESC spd5{ &t56.desc, &t56.desc, 1.0f };
            DML_SCALE_BIAS sb5{ 1.0f, 1.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id5{ &t56.desc, &t56.desc, &sb5 };
            DML_ACTIVATION_SIGMOID_OPERATOR_DESC sd7{ &t7.desc, &t7.desc };
            if (!add_op(pipe_fwd, { DML_OPERATOR_ACTIVATION_TANH, &td0 }, { bb_bind(buf_raw012, batch * D::OUT_STEPS * 3 * 4) }, { bb_bind(buf_out012, batch * D::OUT_STEPS * 3 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_ACTIVATION_SIGMOID, &sd3 }, { bb_bind(buf_raw34, batch * D::OUT_STEPS * 2 * 4) }, { bb_bind(buf_out34, batch * D::OUT_STEPS * 2 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_ACTIVATION_SOFTPLUS, &spd5 }, { bb_bind(buf_raw56, batch * D::OUT_STEPS * 2 * 4) }, { bb_bind(buf_out56_sp, batch * D::OUT_STEPS * 2 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id5 }, { bb_bind(buf_out56_sp, batch * D::OUT_STEPS * 2 * 4) }, { bb_bind(buf_out56, batch * D::OUT_STEPS * 2 * 4) })) return false;
            if (!add_op(pipe_fwd, { DML_OPERATOR_ACTIVATION_SIGMOID, &sd7 }, { bb_bind(buf_raw7, batch * D::OUT_STEPS * 1 * 4) }, { bb_bind(buf_out7, batch * D::OUT_STEPS * 1 * 4) })) return false;

            DML_TENSOR_DESC j_outs[4] = { t012.desc, t34.desc, t56.desc, t7.desc };
            DML_JOIN_OPERATOR_DESC jout{ 4, j_outs, &traw4d.desc, 3 };
            if (!add_op(pipe_fwd, { DML_OPERATOR_JOIN, &jout },
                { bb_bind(buf_out012, batch * D::OUT_STEPS * 3 * 4), bb_bind(buf_out34, batch * D::OUT_STEPS * 2 * 4), bb_bind(buf_out56, batch * D::OUT_STEPS * 2 * 4), bb_bind(buf_out7, batch * D::OUT_STEPS * 1 * 4) },
                { bb_bind(buf_pred, batch * D::OUT_TOTAL * 4) })) return false;

            if (!finalize_pipeline(pipe_fwd)) { return false; }
        }

        {
            uint32_t N_elem = batch * D::OUT_TOTAL;
            float inv_n = 1.0f / static_cast<float>(N_elem);
            Tensor4D tout_flat, tscalar;
            tout_flat.set(1, 1, 1, N_elem);
            tscalar.set(1, 1, 1, 1);

            DML_ELEMENT_WISE_SUBTRACT_OPERATOR_DESC sub_d{ &tout_flat.desc, &tout_flat.desc, &tout_flat.desc };
            if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_SUBTRACT, &sub_d }, { bb_bind(buf_pred, N_elem * 4), bb_bind(buf_target, N_elem * 4) }, { bb_bind(buf_diff, N_elem * 4) })) return false;

            UINT ax3 = 3;
            DML_REDUCE_OPERATOR_DESC red_mse{ DML_REDUCE_FUNCTION_SUM_SQUARE, &tout_flat.desc, &tscalar.desc, 1, &ax3 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &red_mse }, { bb_bind(buf_diff, N_elem * 4) }, { bb_bind(buf_mse_sum, 4) })) return false;

            DML_SCALE_BIAS sb_inv{ inv_n, 0.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_dp{ &tout_flat.desc, &tout_flat.desc, &sb_inv };
            if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_dp }, { bb_bind(buf_diff, N_elem * 4) }, { bb_bind(buf_dp, N_elem * 4) })) return false;

            Tensor4D t4, t012, t34, t56, t7;
            t4.set(1, batch, D::OUT_STEPS, 8);
            t012.set(1, batch, D::OUT_STEPS, 3);
            t34.set(1, batch, D::OUT_STEPS, 2);
            t56.set(1, batch, D::OUT_STEPS, 2);
            t7.set(1, batch, D::OUT_STEPS, 1);
            UINT st1[4] = { 1, 1, 1, 1 };
            UINT off0[4] = { 0, 0, 0, 0 }, sz0[4] = { 1, batch, D::OUT_STEPS, 3 };
            UINT off3[4] = { 0, 0, 0, 3 }, sz3[4] = { 1, batch, D::OUT_STEPS, 2 };
            UINT off5[4] = { 0, 0, 0, 5 }, sz5[4] = { 1, batch, D::OUT_STEPS, 2 };
            UINT off7[4] = { 0, 0, 0, 7 }, sz7[4] = { 1, batch, D::OUT_STEPS, 1 };

            auto sl_pair = [&](const UINT* off, const UINT* sz, const Tensor4D& tout, const DmlBuffer& p_dst, const DmlBuffer& dp_dst, UINT64 dst_bytes) -> bool {
                DML_SLICE_OPERATOR_DESC s_p{ &t4.desc, &tout.desc, 4, off, sz, st1 };
                DML_SLICE_OPERATOR_DESC s_dp{ &t4.desc, &tout.desc, 4, off, sz, st1 };
                return add_op(pipe_bwd, { DML_OPERATOR_SLICE, &s_p }, { bb_bind(buf_pred, N_elem * 4) }, { bb_bind(p_dst, dst_bytes) }) &&
                       add_op(pipe_bwd, { DML_OPERATOR_SLICE, &s_dp }, { bb_bind(buf_dp, N_elem * 4) }, { bb_bind(dp_dst, dst_bytes) });
            };
            if (!sl_pair(off0, sz0, t012, buf_p012, buf_dp012, batch * D::OUT_STEPS * 3 * 4)) return false;
            if (!sl_pair(off3, sz3, t34, buf_p34, buf_dp34, batch * D::OUT_STEPS * 2 * 4)) return false;
            if (!sl_pair(off5, sz5, t56, buf_p56, buf_dp56, batch * D::OUT_STEPS * 2 * 4)) return false;
            if (!sl_pair(off7, sz7, t7, buf_p7, buf_dp7, batch * D::OUT_STEPS * 1 * 4)) return false;
            {
                DML_SLICE_OPERATOR_DESC s_r56{ &t4.desc, &t56.desc, 4, off5, sz5, st1 };
                if (!add_op(pipe_bwd, { DML_OPERATOR_SLICE, &s_r56 }, { bb_bind(buf_raw, N_elem * 4) }, { bb_bind(buf_r56, batch * D::OUT_STEPS * 2 * 4) })) return false;
            }

            {
                UINT64 b012 = batch * D::OUT_STEPS * 3 * 4;
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC msq{ &t012.desc, &t012.desc, &t012.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &msq }, { bb_bind(buf_p012, b012), bb_bind(buf_p012, b012) }, { bb_bind(buf_t012, b012) })) return false;
                DML_SCALE_BIAS sb_omp{ -1.0f, 1.0f };
                DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_omp{ &t012.desc, &t012.desc, &sb_omp };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_omp }, { bb_bind(buf_t012, b012) }, { bb_bind(buf_p012, b012) })) return false;
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC mfin{ &t012.desc, &t012.desc, &t012.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &mfin }, { bb_bind(buf_dp012, b012), bb_bind(buf_p012, b012) }, { bb_bind(buf_dr012, b012) })) return false;
            }
            {
                UINT64 b34 = batch * D::OUT_STEPS * 2 * 4;
                DML_SCALE_BIAS sb_omp{ -1.0f, 1.0f };
                DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_omp{ &t34.desc, &t34.desc, &sb_omp };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_omp }, { bb_bind(buf_p34, b34) }, { bb_bind(buf_t34, b34) })) return false;
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m1{ &t34.desc, &t34.desc, &t34.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m1 }, { bb_bind(buf_p34, b34), bb_bind(buf_t34, b34) }, { bb_bind(buf_p34, b34) })) return false;
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m1 }, { bb_bind(buf_dp34, b34), bb_bind(buf_p34, b34) }, { bb_bind(buf_dr34, b34) })) return false;
            }
            {
                UINT64 b56 = batch * D::OUT_STEPS * 2 * 4;
                DML_ACTIVATION_SIGMOID_OPERATOR_DESC sd{ &t56.desc, &t56.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ACTIVATION_SIGMOID, &sd }, { bb_bind(buf_r56, b56) }, { bb_bind(buf_t56, b56) })) return false;
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m1{ &t56.desc, &t56.desc, &t56.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m1 }, { bb_bind(buf_dp56, b56), bb_bind(buf_t56, b56) }, { bb_bind(buf_dr56, b56) })) return false;
            }
            {
                UINT64 b7 = batch * D::OUT_STEPS * 1 * 4;
                DML_SCALE_BIAS sb_omp{ -1.0f, 1.0f };
                DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_omp{ &t7.desc, &t7.desc, &sb_omp };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_omp }, { bb_bind(buf_p7, b7) }, { bb_bind(buf_t7, b7) })) return false;
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m1{ &t7.desc, &t7.desc, &t7.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m1 }, { bb_bind(buf_p7, b7), bb_bind(buf_t7, b7) }, { bb_bind(buf_p7, b7) })) return false;
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m1 }, { bb_bind(buf_dp7, b7), bb_bind(buf_p7, b7) }, { bb_bind(buf_dr7, b7) })) return false;
            }
            {
                DML_TENSOR_DESC j_ins[4] = { t012.desc, t34.desc, t56.desc, t7.desc };
                DML_JOIN_OPERATOR_DESC jd{ 4, j_ins, &t4.desc, 3 };
                if (!add_op(pipe_bwd, { DML_OPERATOR_JOIN, &jd },
                    { bb_bind(buf_dr012, batch * D::OUT_STEPS * 3 * 4), bb_bind(buf_dr34, batch * D::OUT_STEPS * 2 * 4), bb_bind(buf_dr56, batch * D::OUT_STEPS * 2 * 4), bb_bind(buf_dr7, batch * D::OUT_STEPS * 1 * 4) },
                    { bb_bind(buf_draw, N_elem * 4) })) return false;
            }

            Tensor4D ta3, tdraw, tdw4, tdb4, tw4, tda3;
            ta3.set(1, 1, batch, D::HIDDEN);
            tdraw.set(1, 1, batch, D::OUT_TOTAL);
            tdw4.set(1, 1, D::HIDDEN, D::OUT_TOTAL);
            tdb4.set(1, 1, 1, D::OUT_TOTAL);
            tw4.set(1, 1, D::HIDDEN, D::OUT_TOTAL);
            tda3.set(1, 1, batch, D::HIDDEN);

            DML_GEMM_OPERATOR_DESC g_dw4{ &ta3.desc, &tdraw.desc, nullptr, &tdw4.desc, DML_MATRIX_TRANSFORM_TRANSPOSE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_dw4 }, { bb_bind(buf_a3, batch * D::HIDDEN * 4), bb_bind(buf_draw, N_elem * 4), { nullptr, 0, 0 } }, { pt_bind(buf_grads, w.off_tr_w4, D::TR_W4) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;
            UINT ax2 = 2;
            DML_REDUCE_OPERATOR_DESC r_db4{ DML_REDUCE_FUNCTION_SUM, &tdraw.desc, &tdb4.desc, 1, &ax2 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &r_db4 }, { bb_bind(buf_draw, N_elem * 4) }, { pt_bind(buf_grads, w.off_tr_b4, D::TR_B4) })) return false;
            DML_GEMM_OPERATOR_DESC g_da3{ &tdraw.desc, &tw4.desc, nullptr, &tda3.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_TRANSPOSE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_da3 }, { bb_bind(buf_draw, N_elem * 4), pt_bind(buf_theta, w.off_tr_w4, D::TR_W4), { nullptr, 0, 0 } }, { bb_bind(buf_da3, batch * D::HIDDEN * 4) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;

            auto add_silu_bwd = [&](const DmlBuffer& pre, const DmlBuffer& dact, const DmlBuffer& sig, const DmlBuffer& oms, const DmlBuffer& ps, const DmlBuffer& tmp, const DmlBuffer& dpre, UINT dim) -> bool {
                UINT64 bytes = batch * dim * 4;
                Tensor4D t; t.set(1, 1, batch, dim);
                DML_ACTIVATION_SIGMOID_OPERATOR_DESC sd{ &t.desc, &t.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ACTIVATION_SIGMOID, &sd }, { bb_bind(pre, bytes) }, { bb_bind(sig, bytes) })) return false;
                DML_SCALE_BIAS sb{ -1.0f, 1.0f };
                DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id{ &t.desc, &t.desc, &sb };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id }, { bb_bind(sig, bytes) }, { bb_bind(oms, bytes) })) return false;
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m{ &t.desc, &t.desc, &t.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m }, { bb_bind(pre, bytes), bb_bind(sig, bytes) }, { bb_bind(ps, bytes) })) return false;
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m }, { bb_bind(ps, bytes), bb_bind(oms, bytes) }, { bb_bind(tmp, bytes) })) return false;
                DML_ELEMENT_WISE_ADD_OPERATOR_DESC a{ &t.desc, &t.desc, &t.desc };
                if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_ADD, &a }, { bb_bind(sig, bytes), bb_bind(tmp, bytes) }, { bb_bind(ps, bytes) })) return false;
                return add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m }, { bb_bind(dact, bytes), bb_bind(ps, bytes) }, { bb_bind(dpre, bytes) });
            };

            if (!add_silu_bwd(buf_pre3, buf_da3, buf_sig3, buf_om_s3, buf_p3_s3, buf_tmp3, buf_dpre3, D::HIDDEN)) return false;

            Tensor4D ta2, tdw3, tdb3, tw3, tda2;
            ta2.set(1, 1, batch, D::HIDDEN);
            tdw3.set(1, 1, D::HIDDEN, D::HIDDEN);
            tdb3.set(1, 1, 1, D::HIDDEN);
            tw3.set(1, 1, D::HIDDEN, D::HIDDEN);
            tda2.set(1, 1, batch, D::HIDDEN);

            DML_GEMM_OPERATOR_DESC g_dw3{ &ta2.desc, &tda3.desc, nullptr, &tdw3.desc, DML_MATRIX_TRANSFORM_TRANSPOSE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_dw3 }, { bb_bind(buf_a2, batch * D::HIDDEN * 4), bb_bind(buf_dpre3, batch * D::HIDDEN * 4), { nullptr, 0, 0 } }, { pt_bind(buf_grads, w.off_tr_w3, D::TR_W3) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;
            DML_REDUCE_OPERATOR_DESC r_db3{ DML_REDUCE_FUNCTION_SUM, &tda3.desc, &tdb3.desc, 1, &ax2 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &r_db3 }, { bb_bind(buf_dpre3, batch * D::HIDDEN * 4) }, { pt_bind(buf_grads, w.off_tr_b3, D::TR_B3) })) return false;
            DML_GEMM_OPERATOR_DESC g_da2{ &tda3.desc, &tw3.desc, nullptr, &tda2.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_TRANSPOSE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_da2 }, { bb_bind(buf_dpre3, batch * D::HIDDEN * 4), pt_bind(buf_theta, w.off_tr_w3, D::TR_W3), { nullptr, 0, 0 } }, { bb_bind(buf_da2, batch * D::HIDDEN * 4) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;

            if (!add_silu_bwd(buf_pre2, buf_da2, buf_sig2, buf_om_s2, buf_p2_s2, buf_tmp2, buf_dpre2, D::HIDDEN)) return false;

            Tensor4D ta1, tdw2, tdb2, tw2, tda1;
            ta1.set(1, 1, batch, D::TRUNK_H);
            tdw2.set(1, 1, D::TRUNK_H, D::HIDDEN);
            tdb2.set(1, 1, 1, D::HIDDEN);
            tw2.set(1, 1, D::TRUNK_H, D::HIDDEN);
            tda1.set(1, 1, batch, D::TRUNK_H);

            DML_GEMM_OPERATOR_DESC g_dw2{ &ta1.desc, &tda2.desc, nullptr, &tdw2.desc, DML_MATRIX_TRANSFORM_TRANSPOSE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_dw2 }, { bb_bind(buf_a1, batch * D::TRUNK_H * 4), bb_bind(buf_dpre2, batch * D::HIDDEN * 4), { nullptr, 0, 0 } }, { pt_bind(buf_grads, w.off_tr_w2, D::TR_W2) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;
            DML_REDUCE_OPERATOR_DESC r_db2{ DML_REDUCE_FUNCTION_SUM, &tda2.desc, &tdb2.desc, 1, &ax2 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &r_db2 }, { bb_bind(buf_dpre2, batch * D::HIDDEN * 4) }, { pt_bind(buf_grads, w.off_tr_b2, D::TR_B2) })) return false;
            DML_GEMM_OPERATOR_DESC g_da1{ &tda2.desc, &tw2.desc, nullptr, &tda1.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_TRANSPOSE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_da1 }, { bb_bind(buf_dpre2, batch * D::HIDDEN * 4), pt_bind(buf_theta, w.off_tr_w2, D::TR_W2), { nullptr, 0, 0 } }, { bb_bind(buf_da1, batch * D::TRUNK_H * 4) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;

            if (!add_silu_bwd(buf_pre1, buf_da1, buf_sig1, buf_om_s1, buf_p1_s1, buf_tmp1, buf_dpre1, D::TRUNK_H)) return false;

            Tensor4D tz0, tdw1, tdb1;
            tz0.set(1, 1, batch, D::TRUNK_H);
            tdw1.set(1, 1, D::TRUNK_H, D::TRUNK_H);
            tdb1.set(1, 1, 1, D::TRUNK_H);

            DML_GEMM_OPERATOR_DESC g_dw1{ &tz0.desc, &tda1.desc, nullptr, &tdw1.desc, DML_MATRIX_TRANSFORM_TRANSPOSE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 0.0f, nullptr };
            if (!add_op(pipe_bwd, { DML_OPERATOR_GEMM, &g_dw1 }, { bb_bind(buf_z0, batch * D::TRUNK_H * 4), bb_bind(buf_dpre1, batch * D::TRUNK_H * 4), { nullptr, 0, 0 } }, { pt_bind(buf_grads, w.off_tr_w1, D::TR_W1) }, { DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_BUFFER, DML_BINDING_TYPE_NONE })) return false;
            DML_REDUCE_OPERATOR_DESC r_db1{ DML_REDUCE_FUNCTION_SUM, &tda1.desc, &tdb1.desc, 1, &ax2 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &r_db1 }, { bb_bind(buf_dpre1, batch * D::TRUNK_H * 4) }, { pt_bind(buf_grads, w.off_tr_b1, D::TR_B1) })) return false;

            Tensor4D tp;
            tp.set(1, 1, 1, D::PLASTIC_TOTAL);
            UINT64 pbytes = D::PLASTIC_TOTAL * 4;
            DML_ELEMENT_WISE_SUBTRACT_OPERATOR_DESC sub_th{ &tp.desc, &tp.desc, &tp.desc };
            if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_SUBTRACT, &sub_th }, { bb_bind(buf_theta, pbytes), bb_bind(buf_anchor, pbytes) }, { bb_bind(buf_diff_th, pbytes) })) return false;

            DML_REDUCE_OPERATOR_DESC r_l2{ DML_REDUCE_FUNCTION_SUM_SQUARE, &tp.desc, &tscalar.desc, 1, &ax3 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &r_l2 }, { bb_bind(buf_diff_th, pbytes) }, { bb_bind(buf_l2_sq, 4) })) return false;

            DML_SCALE_BIAS sb_lam{ cfg.lambda_anchor, 0.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_lam{ &tp.desc, &tp.desc, &sb_lam };
            if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_lam }, { bb_bind(buf_diff_th, pbytes) }, { bb_bind(buf_lam_diff, pbytes) })) return false;

            DML_ELEMENT_WISE_ADD_OPERATOR_DESC add_l2{ &tp.desc, &tp.desc, &tp.desc };
            if (!add_op(pipe_bwd, { DML_OPERATOR_ELEMENT_WISE_ADD, &add_l2 }, { bb_bind(buf_grads, pbytes), bb_bind(buf_lam_diff, pbytes) }, { bb_bind(buf_grads, pbytes) })) return false;

            DML_REDUCE_OPERATOR_DESC r_nsq{ DML_REDUCE_FUNCTION_SUM_SQUARE, &tp.desc, &tscalar.desc, 1, &ax3 };
            if (!add_op(pipe_bwd, { DML_OPERATOR_REDUCE, &r_nsq }, { bb_bind(buf_grads, pbytes) }, { bb_bind(buf_norm_sq, 4) })) return false;

            if (!finalize_pipeline(pipe_bwd)) { return false; }
        }

        {
            Tensor4D tp, ts_bcast;
            tp.set(1, 1, 1, D::PLASTIC_TOTAL);
            ts_bcast.set_strided(1, 1, 1, D::PLASTIC_TOTAL, 0, 0, 0, 0, 4);
            UINT64 pbytes = D::PLASTIC_TOTAL * 4;

            DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m_clip{ &tp.desc, &ts_bcast.desc, &tp.desc };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m_clip }, { bb_bind(buf_grads, pbytes), { buf_opt_scalars.res.Get(), 0, 4 } }, { bb_bind(buf_gclip, pbytes) })) return false;

            DML_SCALE_BIAS sb_b1{ cfg.beta1, 0.0f }, sb_1mb1{ 1.0f - cfg.beta1, 0.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_b1{ &tp.desc, &tp.desc, &sb_b1 }, id_1mb1{ &tp.desc, &tp.desc, &sb_1mb1 };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_b1 }, { bb_bind(buf_m, pbytes) }, { bb_bind(buf_m1, pbytes) })) return false;
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_1mb1 }, { bb_bind(buf_gclip, pbytes) }, { bb_bind(buf_m2, pbytes) })) return false;
            DML_ELEMENT_WISE_ADD_OPERATOR_DESC add_m{ &tp.desc, &tp.desc, &tp.desc };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_ADD, &add_m }, { bb_bind(buf_m1, pbytes), bb_bind(buf_m2, pbytes) }, { bb_bind(buf_m_tmp, pbytes) })) return false;

            DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m_gsq{ &tp.desc, &tp.desc, &tp.desc };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m_gsq }, { bb_bind(buf_gclip, pbytes), bb_bind(buf_gclip, pbytes) }, { bb_bind(buf_gsq, pbytes) })) return false;
            DML_SCALE_BIAS sb_b2{ cfg.beta2, 0.0f }, sb_1mb2{ 1.0f - cfg.beta2, 0.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_b2{ &tp.desc, &tp.desc, &sb_b2 }, id_1mb2{ &tp.desc, &tp.desc, &sb_1mb2 };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_b2 }, { bb_bind(buf_v, pbytes) }, { bb_bind(buf_v1, pbytes) })) return false;
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_1mb2 }, { bb_bind(buf_gsq, pbytes) }, { bb_bind(buf_v2, pbytes) })) return false;
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_ADD, &add_m }, { bb_bind(buf_v1, pbytes), bb_bind(buf_v2, pbytes) }, { bb_bind(buf_v_tmp, pbytes) })) return false;

            DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m_bc1{ &tp.desc, &ts_bcast.desc, &tp.desc };
            DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC m_bc2{ &tp.desc, &ts_bcast.desc, &tp.desc };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m_bc1 }, { bb_bind(buf_m_tmp, pbytes), { buf_opt_scalars.res.Get(), 16, 4 } }, { bb_bind(buf_mhat, pbytes) })) return false;
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &m_bc2 }, { bb_bind(buf_v_tmp, pbytes), { buf_opt_scalars.res.Get(), 32, 4 } }, { bb_bind(buf_vhat, pbytes) })) return false;

            DML_ELEMENT_WISE_SQRT_OPERATOR_DESC sq_v{ &tp.desc, &tp.desc, nullptr };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_SQRT, &sq_v }, { bb_bind(buf_vhat, pbytes) }, { bb_bind(buf_vsqrt, pbytes) })) return false;
            DML_SCALE_BIAS sb_eps{ 1.0f, cfg.eps };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_eps{ &tp.desc, &tp.desc, &sb_eps };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_eps }, { bb_bind(buf_vsqrt, pbytes) }, { bb_bind(buf_denom, pbytes) })) return false;

            DML_ELEMENT_WISE_DIVIDE_OPERATOR_DESC div_s{ &tp.desc, &tp.desc, &tp.desc };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_DIVIDE, &div_s }, { bb_bind(buf_mhat, pbytes), bb_bind(buf_denom, pbytes) }, { bb_bind(buf_step_div, pbytes) })) return false;
            DML_SCALE_BIAS sb_lr{ cfg.lr, 0.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_lr{ &tp.desc, &tp.desc, &sb_lr };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_lr }, { bb_bind(buf_step_div, pbytes) }, { bb_bind(buf_step, pbytes) })) return false;

            DML_SCALE_BIAS sb_dec{ 1.0f - cfg.lr * cfg.weight_decay, 0.0f };
            DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC id_dec{ &tp.desc, &tp.desc, &sb_dec };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &id_dec }, { bb_bind(buf_theta, pbytes) }, { bb_bind(buf_decay_th, pbytes) })) return false;
            DML_ELEMENT_WISE_SUBTRACT_OPERATOR_DESC sub_th{ &tp.desc, &tp.desc, &tp.desc };
            if (!add_op(pipe_adamw, { DML_OPERATOR_ELEMENT_WISE_SUBTRACT, &sub_th }, { bb_bind(buf_decay_th, pbytes), bb_bind(buf_step, pbytes) }, { bb_bind(buf_theta_tmp, pbytes) })) return false;

            if (!finalize_pipeline(pipe_adamw, false)) { return false; }

            if (FAILED(d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pipe_adamw.alloc)))) return false;
            if (FAILED(d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pipe_adamw.alloc.Get(), nullptr, IID_PPV_ARGS(&pipe_adamw.list)))) return false;
            ID3D12DescriptorHeap* heaps[] = { pipe_adamw.heap.Get() };
            pipe_adamw.list->SetDescriptorHeaps(1, heaps);
            D3D12_RESOURCE_BARRIER uav{};
            uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            for (size_t i = 0; i < pipe_adamw.ops.size(); ++i) {
                cmd_recorder->RecordDispatch(pipe_adamw.list.Get(), pipe_adamw.ops[i].compiled.Get(), pipe_adamw.ops[i].bt.Get());
                pipe_adamw.list->ResourceBarrier(1, &uav);
            }
            auto copy_back = [&](const DmlBuffer& src, const DmlBuffer& dst) {
                D3D12_RESOURCE_BARRIER b[2] = {};
                b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[0].Transition = { src.res.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE };
                b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[1].Transition = { dst.res.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST };
                pipe_adamw.list->ResourceBarrier(2, b);
                pipe_adamw.list->CopyBufferRegion(dst.res.Get(), 0, src.res.Get(), 0, pbytes);
                b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                pipe_adamw.list->ResourceBarrier(2, b);
            };
            copy_back(buf_m_tmp, buf_m);
            copy_back(buf_v_tmp, buf_v);
            copy_back(buf_theta_tmp, buf_theta);
            pipe_adamw.list->Close();
        }
        return true;
    }

    bool sync_plastic_weights(const strafenet::StrafeNetWeights& w) {
        return upload_buffer(buf_theta, w.plastic_theta.data(), strafenet::ModelDims::PLASTIC_TOTAL * 4);
    }

    bool download_plastic_weights(strafenet::StrafeNetWeights& w) {
        return download_buffer(buf_theta, w.plastic_theta.data(), strafenet::ModelDims::PLASTIC_TOTAL * 4);
    }

    void forward_sync(const float* x_seq, const float* x_ctx, float* out, uint32_t batch) {
        using D = strafenet::ModelDims;
        if (batch == 0 || batch > active_batch) return;
        if (batch == active_batch) {
            upload_buffer(buf_xseq, x_seq, batch * D::SEQ_LEN * D::D_SEQ * 4);
            upload_buffer(buf_xctx, x_ctx, batch * D::D_CTX * 4);
            run_pipeline(pipe_fwd);
            download_buffer(buf_pred, out, batch * D::OUT_TOTAL * 4);
            return;
        }
        std::vector<float> px(active_batch * D::SEQ_LEN * D::D_SEQ, 0.0f);
        std::vector<float> pc(active_batch * D::D_CTX, 0.0f);
        std::vector<float> po(active_batch * D::OUT_TOTAL, 0.0f);
        std::memcpy(px.data(), x_seq, batch * D::SEQ_LEN * D::D_SEQ * 4);
        std::memcpy(pc.data(), x_ctx, batch * D::D_CTX * 4);
        upload_buffer(buf_xseq, px.data(), active_batch * D::SEQ_LEN * D::D_SEQ * 4);
        upload_buffer(buf_xctx, pc.data(), active_batch * D::D_CTX * 4);
        run_pipeline(pipe_fwd);
        download_buffer(buf_pred, po.data(), active_batch * D::OUT_TOTAL * 4);
        std::memcpy(out, po.data(), batch * D::OUT_TOTAL * 4);
    }

    strafenet::TrainStepMetrics train_step_sync(
        const float* x_seq,
        const float* x_ctx,
        const float* target,
        uint32_t batch,
        const strafenet::TrainConfig& cfg
    ) {
        using D = strafenet::ModelDims;
        upload_buffer(buf_xseq, x_seq, batch * D::SEQ_LEN * D::D_SEQ * 4);
        upload_buffer(buf_xctx, x_ctx, batch * D::D_CTX * 4);
        upload_buffer(buf_target, target, batch * D::OUT_TOTAL * 4);
        run_pipeline(pipe_fwd);
        run_pipeline(pipe_bwd);

        float mse_sq = 0.0f, l2_sq = 0.0f, norm_sq = 0.0f;
        download_buffer(buf_mse_sum, &mse_sq, 4);
        download_buffer(buf_l2_sq, &l2_sq, 4);
        download_buffer(buf_norm_sq, &norm_sq, 4);

        strafenet::TrainStepMetrics m{};
        m.mse_loss = 0.5f * mse_sq / static_cast<float>(batch * D::OUT_TOTAL);
        m.l2_anchor_loss = 0.5f * cfg.lambda_anchor * l2_sq;
        m.total_loss = m.mse_loss + m.l2_anchor_loss;
        m.grad_norm = std::sqrt(norm_sq);

        if (!std::isfinite(norm_sq) || !std::isfinite(mse_sq)) {
            skipped_nonfinite++;
            m.applied = false;
            return m;
        }

        float clip_scale = (m.grad_norm > cfg.max_grad_norm) ? (cfg.max_grad_norm / (m.grad_norm + 1e-12f)) : 1.0f;
        opt_step++;
        float inv_bc1 = 1.0f / (1.0f - std::pow(cfg.beta1, static_cast<float>(opt_step)));
        float inv_bc2 = 1.0f / (1.0f - std::pow(cfg.beta2, static_cast<float>(opt_step)));

        float scalars[12] = {};
        scalars[0] = clip_scale;
        scalars[4] = inv_bc1;
        scalars[8] = inv_bc2;
        upload_buffer(buf_opt_scalars, scalars, sizeof(scalars));
        run_pipeline(pipe_adamw);
        m.applied = true;
        return m;
    }

    void destroy() {
        release_model_buffers();
        upload_buf.res.Reset();
        readback_buf.res.Reset();
        if (fence_event) { CloseHandle(fence_event); fence_event = nullptr; }
        cmd_recorder.Reset();
        dml_device.Reset();
        fence.Reset();
        cmd_list.Reset();
        cmd_alloc.Reset();
        cmd_queue.Reset();
        d3d_device.Reset();
        api.unload();
    }
};

}



namespace strafenet_rt {

enum class BackendKind {
    Auto = 0,
    Cuda = 1,
    DirectML = 2,
    CpuAvx2 = 3,
    CpuSse2 = 4,
    CpuScalar = 5
};

inline const char* backend_name(BackendKind k) {
    switch (k) {
    case BackendKind::Auto: return "Auto";
    case BackendKind::Cuda: return "CUDA";
    case BackendKind::DirectML: return "DirectML";
    case BackendKind::CpuAvx2: return "CPU_AVX2";
    case BackendKind::CpuSse2: return "CPU_SSE2";
    case BackendKind::CpuScalar: return "CPU_Scalar";
    default: return "Unknown";
    }
}

struct ComputeDispatcher {
    BackendKind preference = BackendKind::Auto;
    BackendKind best_gpu = BackendKind::CpuScalar;
    BackendKind best_cpu = BackendKind::CpuScalar;

    bool has_cuda = false;
    bool has_dml = false;
    bool has_avx2 = false;

    strafenet_cuda::CudaBackend cuda;
    strafenet_dml::DmlBackend dml;
    strafenet_cpu::CpuWorkspace cpu_ws;
    strafenet::AdamWState cpu_opt;
    strafenet::TrainConfig train_cfg{};

    strafenet::StrafeNetWeights weights;
    bool weights_loaded = false;
    uint32_t gpu_batch_cap = 64;

    bool init(BackendKind pref = BackendKind::Auto, uint32_t max_batch_cap = 64) {
        preference = pref;
        gpu_batch_cap = max_batch_cap;
        has_avx2 = strafenet_cpu::is_avx2_fma_supported();
        best_cpu = has_avx2 ? BackendKind::CpuAvx2 : BackendKind::CpuSse2;

        if (pref == BackendKind::Auto || pref == BackendKind::Cuda) {
            has_cuda = cuda.init(0);
        }
        if ((pref == BackendKind::Auto && !has_cuda) || pref == BackendKind::DirectML) {
            has_dml = dml.init(-1);
        }
        if (has_cuda) {
            best_gpu = BackendKind::Cuda;
        } else if (has_dml) {
            best_gpu = BackendKind::DirectML;
        } else {
            best_gpu = best_cpu;
        }
        cpu_opt.reset();
        return true;
    }

    bool load_weights(const strafenet::StrafeNetWeights& w, uint32_t dml_batch = 64) {
        weights = w;
        weights.compute_offsets();
        weights_loaded = true;
        cpu_opt.reset();
        if (has_cuda) {
            if (!cuda.upload_model(weights, gpu_batch_cap)) { cuda.destroy(); has_cuda = false; }
        }
        if (has_dml) {
            if (!dml.build_pipelines(weights, dml_batch, train_cfg)) { dml.destroy(); has_dml = false; }
        }
        return true;
    }

    BackendKind resolve_infer_backend(uint32_t batch) const {
        if (preference != BackendKind::Auto) {
            if (preference == BackendKind::Cuda && has_cuda) return BackendKind::Cuda;
            if (preference == BackendKind::DirectML && has_dml) return BackendKind::DirectML;
            if (preference == BackendKind::CpuAvx2 && has_avx2) return BackendKind::CpuAvx2;
            if (preference == BackendKind::CpuSse2) return BackendKind::CpuSse2;
            if (preference == BackendKind::CpuScalar) return BackendKind::CpuScalar;
            return best_cpu;
        }
        if (batch <= 4) {
            return best_cpu;
        }
        if (has_cuda || has_dml) {
            return has_cuda ? BackendKind::Cuda : BackendKind::DirectML;
        }
        return best_cpu;
    }

    BackendKind resolve_train_backend() const {
        if (preference != BackendKind::Auto) {
            if (preference == BackendKind::Cuda && has_cuda) return BackendKind::Cuda;
            if (preference == BackendKind::DirectML && has_dml) return BackendKind::DirectML;
            if (preference == BackendKind::CpuAvx2 && has_avx2) return BackendKind::CpuAvx2;
            return best_cpu;
        }
        if (has_cuda) return BackendKind::Cuda;
        if (has_dml) return BackendKind::DirectML;
        return best_cpu;
    }

    BackendKind forward(const float* x_seq, const float* x_ctx, float* out, uint32_t batch, BackendKind override_backend = BackendKind::Auto) {
        BackendKind b = (override_backend == BackendKind::Auto) ? resolve_infer_backend(batch) : override_backend;
        if (b == BackendKind::Cuda && has_cuda && batch <= cuda.max_batch) {
            cuda.forward_sync(weights, x_seq, x_ctx, out, batch);
            return BackendKind::Cuda;
        }
        if (b == BackendKind::DirectML && has_dml) {
            if (batch > dml.active_batch || dml.active_arch != weights.arch) {
                dml.build_pipelines(weights, std::max(batch, 32u), train_cfg);
            }
            dml.forward_sync(x_seq, x_ctx, out, batch);
            return BackendKind::DirectML;
        }
        if (b == BackendKind::CpuAvx2 && has_avx2) {
            strafenet_cpu::avx2_forward(weights, x_seq, x_ctx, out, batch, cpu_ws);
            return BackendKind::CpuAvx2;
        }
        if (b == BackendKind::CpuSse2) {
            strafenet_cpu::sse2_forward(weights, x_seq, x_ctx, out, batch, cpu_ws);
            return BackendKind::CpuSse2;
        }
        strafenet::ref_forward(weights, x_seq, x_ctx, out, batch);
        return BackendKind::CpuScalar;
    }

    strafenet::TrainStepMetrics train_step(
        const float* x_seq,
        const float* x_ctx,
        const float* target,
        uint32_t batch,
        BackendKind* used_backend = nullptr,
        bool sync_host_weights = true
    ) {
        BackendKind b = resolve_train_backend();
        if (used_backend) *used_backend = b;

        if (b == BackendKind::Cuda && has_cuda) {
            auto m = cuda.train_step_sync(weights, x_seq, x_ctx, target, batch, train_cfg);
            if (sync_host_weights && m.applied) {
                cuda.download_plastic_weights(weights);
            }
            return m;
        }
        if (b == BackendKind::DirectML && has_dml) {
            if (dml.active_batch != batch || dml.active_arch != weights.arch) {
                dml.build_pipelines(weights, batch, train_cfg);
            }
            auto m = dml.train_step_sync(x_seq, x_ctx, target, batch, train_cfg);
            if (sync_host_weights && m.applied) {
                dml.download_plastic_weights(weights);
            }
            return m;
        }
        if (b == BackendKind::CpuAvx2 && has_avx2) {
            return strafenet_cpu::avx2_train_step(weights, cpu_opt, x_seq, x_ctx, target, batch, train_cfg, cpu_ws);
        }
        return strafenet::ref_train_step(weights, cpu_opt, x_seq, x_ctx, target, batch, train_cfg);
    }

    void sync_plastic_weights(const std::vector<float>& plastic) {
        if (plastic.size() != weights.plastic_theta.size()) return;
        weights.plastic_theta = plastic;
        if (has_cuda) {
            cuda.sync_plastic_weights(weights);
        }
        if (has_dml) {
            dml.sync_plastic_weights(weights);
        }
    }

    ~ComputeDispatcher() {
        destroy();
    }

    void destroy() {
        if (has_cuda) { cuda.destroy(); has_cuda = false; }
        if (has_dml) { dml.destroy(); has_dml = false; }
    }
};

}
using strafenet::ArchType;
using strafenet::ModelDims;
using strafenet::StrafeNetWeights;
using strafenet::TrainConfig;
using strafenet::TrainStepMetrics;
using strafenet_rt::BackendKind;
using strafenet_rt::ComputeDispatcher;
using strafenet_rt::backend_name;

}
