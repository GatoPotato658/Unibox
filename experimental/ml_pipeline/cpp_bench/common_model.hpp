#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace bench_model {

struct ModelConfig {
    static constexpr uint32_t IN_DIM = 64;
    static constexpr uint32_t H1 = 128;
    static constexpr uint32_t H2 = 256;
    static constexpr uint32_t H3 = 128;
    static constexpr uint32_t GRU_H = 128;
    static constexpr uint32_t OUT_DIM = 64;

    static constexpr uint32_t W1_SIZE = IN_DIM * H1;
    static constexpr uint32_t B1_SIZE = H1;
    static constexpr uint32_t W2_SIZE = H1 * H2;
    static constexpr uint32_t B2_SIZE = H2;
    static constexpr uint32_t W3_SIZE = H2 * H3;
    static constexpr uint32_t B3_SIZE = H3;
    static constexpr uint32_t W_IH_SIZE = H3 * (3 * GRU_H);
    static constexpr uint32_t B_IH_SIZE = 3 * GRU_H;
    static constexpr uint32_t W_HH_SIZE = GRU_H * (3 * GRU_H);
    static constexpr uint32_t B_HH_SIZE = 3 * GRU_H;
    static constexpr uint32_t W_OUT_SIZE = GRU_H * OUT_DIM;
    static constexpr uint32_t B_OUT_SIZE = OUT_DIM;

    static constexpr uint32_t TOTAL_PARAMS =
        W1_SIZE + B1_SIZE +
        W2_SIZE + B2_SIZE +
        W3_SIZE + B3_SIZE +
        W_IH_SIZE + B_IH_SIZE +
        W_HH_SIZE + B_HH_SIZE +
        W_OUT_SIZE + B_OUT_SIZE;
};

struct ModelWeights {
    std::vector<float> params;
    size_t off_w1 = 0;
    size_t off_b1 = 0;
    size_t off_w2 = 0;
    size_t off_b2 = 0;
    size_t off_w3 = 0;
    size_t off_b3 = 0;
    size_t off_w_ih = 0;
    size_t off_b_ih = 0;
    size_t off_w_hh = 0;
    size_t off_b_hh = 0;
    size_t off_w_out = 0;
    size_t off_b_out = 0;

    void init(uint32_t seed = 1337u) {
        params.resize(ModelConfig::TOTAL_PARAMS);
        size_t o = 0;
        off_w1 = o; o += ModelConfig::W1_SIZE;
        off_b1 = o; o += ModelConfig::B1_SIZE;
        off_w2 = o; o += ModelConfig::W2_SIZE;
        off_b2 = o; o += ModelConfig::B2_SIZE;
        off_w3 = o; o += ModelConfig::W3_SIZE;
        off_b3 = o; o += ModelConfig::B3_SIZE;
        off_w_ih = o; o += ModelConfig::W_IH_SIZE;
        off_b_ih = o; o += ModelConfig::B_IH_SIZE;
        off_w_hh = o; o += ModelConfig::W_HH_SIZE;
        off_b_hh = o; o += ModelConfig::B_HH_SIZE;
        off_w_out = o; o += ModelConfig::W_OUT_SIZE;
        off_b_out = o; o += ModelConfig::B_OUT_SIZE;

        uint32_t s = seed;
        auto next_f = [&]() -> float {
            s = s * 1664525u + 1013904223u;
            int32_t v = static_cast<int32_t>(s >> 8) - 8388608;
            return static_cast<float>(v) * (0.08f / 8388608.0f);
        };
        for (size_t i = 0; i < params.size(); ++i) {
            params[i] = next_f();
        }
    }
};

inline float ref_sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

inline float ref_silu(float x) {
    return x * ref_sigmoid(x);
}

inline float ref_tanh(float x) {
    return std::tanh(x);
}

inline void ref_sgemm_bias_act(
    const float* A,
    const float* B,
    const float* bias,
    float* C,
    uint32_t M,
    uint32_t N,
    uint32_t K,
    bool silu
) {
    for (uint32_t m = 0; m < M; ++m) {
        const float* a_row = A + m * K;
        float* c_row = C + m * N;
        for (uint32_t n = 0; n < N; ++n) {
            float acc = bias ? bias[n] : 0.0f;
            for (uint32_t k = 0; k < K; ++k) {
                acc += a_row[k] * B[k * N + n];
            }
            if (silu) {
                acc = ref_silu(acc);
            }
            c_row[n] = acc;
        }
    }
}

inline void ref_gru_step(
    const float* x,
    const float* h_prev,
    const float* w_ih,
    const float* b_ih,
    const float* w_hh,
    const float* b_hh,
    float* h_out,
    uint32_t batch,
    uint32_t in_dim,
    uint32_t hidden_dim
) {
    uint32_t H3 = 3 * hidden_dim;
    std::vector<float> gx(batch * H3);
    std::vector<float> gh(batch * H3);
    ref_sgemm_bias_act(x, w_ih, b_ih, gx.data(), batch, H3, in_dim, false);
    ref_sgemm_bias_act(h_prev, w_hh, b_hh, gh.data(), batch, H3, hidden_dim, false);
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t i = 0; i < hidden_dim; ++i) {
            float xz = gx[b * H3 + i];
            float xr = gx[b * H3 + hidden_dim + i];
            float xn = gx[b * H3 + 2 * hidden_dim + i];
            float hz = gh[b * H3 + i];
            float hr = gh[b * H3 + hidden_dim + i];
            float hn = gh[b * H3 + 2 * hidden_dim + i];
            float z = ref_sigmoid(xz + hz);
            float r = ref_sigmoid(xr + hr);
            float n = ref_tanh(xn + r * hn);
            h_out[b * hidden_dim + i] = (1.0f - z) * n + z * h_prev[b * hidden_dim + i];
        }
    }
}

inline void ref_forward(
    const ModelWeights& w,
    const float* x,
    const float* h_init,
    float* out,
    float* h_next,
    uint32_t batch
) {
    const float* p = w.params.data();
    std::vector<float> a1(batch * ModelConfig::H1);
    std::vector<float> a2(batch * ModelConfig::H2);
    std::vector<float> a3(batch * ModelConfig::H3);
    ref_sgemm_bias_act(x, p + w.off_w1, p + w.off_b1, a1.data(), batch, ModelConfig::H1, ModelConfig::IN_DIM, true);
    ref_sgemm_bias_act(a1.data(), p + w.off_w2, p + w.off_b2, a2.data(), batch, ModelConfig::H2, ModelConfig::H1, true);
    ref_sgemm_bias_act(a2.data(), p + w.off_w3, p + w.off_b3, a3.data(), batch, ModelConfig::H3, ModelConfig::H2, true);
    ref_gru_step(
        a3.data(), h_init,
        p + w.off_w_ih, p + w.off_b_ih,
        p + w.off_w_hh, p + w.off_b_hh,
        h_next, batch, ModelConfig::H3, ModelConfig::GRU_H
    );
    ref_sgemm_bias_act(h_next, p + w.off_w_out, p + w.off_b_out, out, batch, ModelConfig::OUT_DIM, ModelConfig::GRU_H, false);
}

inline void ref_adamw_step(
    float* params,
    const float* grads,
    float* m,
    float* v,
    uint32_t n,
    float lr,
    float beta1,
    float beta2,
    float eps,
    float weight_decay,
    float inv_bc1,
    float inv_bc2
) {
    float one_m_b1 = 1.0f - beta1;
    float one_m_b2 = 1.0f - beta2;
    float decay = 1.0f - lr * weight_decay;
    for (uint32_t i = 0; i < n; ++i) {
        float g = grads[i];
        float mi = beta1 * m[i] + one_m_b1 * g;
        float vi = beta2 * v[i] + one_m_b2 * g * g;
        m[i] = mi;
        v[i] = vi;
        float m_hat = mi * inv_bc1;
        float v_hat = vi * inv_bc2;
        params[i] = params[i] * decay - lr * m_hat / (std::sqrt(v_hat) + eps);
    }
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
