#pragma once

#include "common_model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <intrin.h>
#include <vector>

namespace bench_cpu {

struct CpuCapabilities {
    char vendor[13] = {};
    char brand[49] = {};
    bool avx = false;
    bool fma = false;
    bool osxsave = false;
    bool avx2 = false;
    bool sse2 = false;
};

inline CpuCapabilities detect_cpu() {
    CpuCapabilities cap{};
    int info[4] = {};
    __cpuid(info, 0);
    int max_leaf = info[0];
    std::memcpy(cap.vendor + 0, &info[1], 4);
    std::memcpy(cap.vendor + 4, &info[3], 4);
    std::memcpy(cap.vendor + 8, &info[2], 4);
    cap.vendor[12] = '\0';

    if (max_leaf >= 1) {
        __cpuid(info, 1);
        cap.sse2 = (info[3] & (1 << 26)) != 0;
        cap.fma = (info[2] & (1 << 12)) != 0;
        cap.osxsave = (info[2] & (1 << 27)) != 0;
        cap.avx = (info[2] & (1 << 28)) != 0;
    }

    bool ymm_os_ok = false;
    if (cap.osxsave) {
        uint64_t xcr0 = _xgetbv(0);
        ymm_os_ok = (xcr0 & 0x6) == 0x6;
    }

    if (max_leaf >= 7 && ymm_os_ok) {
        __cpuidex(info, 7, 0);
        cap.avx2 = (info[1] & (1 << 5)) != 0;
    } else {
        cap.avx = false;
        cap.fma = false;
    }

    __cpuid(info, static_cast<int>(0x80000000u));
    uint32_t max_ext = static_cast<uint32_t>(info[0]);
    if (max_ext >= 0x80000004u) {
        __cpuid(reinterpret_cast<int*>(cap.brand + 0), static_cast<int>(0x80000002u));
        __cpuid(reinterpret_cast<int*>(cap.brand + 16), static_cast<int>(0x80000003u));
        __cpuid(reinterpret_cast<int*>(cap.brand + 32), static_cast<int>(0x80000004u));
        cap.brand[48] = '\0';
    }
    return cap;
}

inline bool is_avx2_fma_supported() {
    static const bool supported = []() {
        CpuCapabilities c = detect_cpu();
        return c.avx2 && c.fma && c.avx;
    }();
    return supported;
}

inline __m256 avx2_exp_ps(__m256 x) {
    const __m256 hi = _mm256_set1_ps(88.3762626647949f);
    const __m256 lo = _mm256_set1_ps(-87.3365447504f);
    x = _mm256_min_ps(x, hi);
    x = _mm256_max_ps(x, lo);

    const __m256 log2ef = _mm256_set1_ps(1.44269504088896341f);
    const __m256 ln2_hi = _mm256_set1_ps(0.693359375f);
    const __m256 ln2_lo = _mm256_set1_ps(-2.12194440e-4f);

    __m256 fx = _mm256_round_ps(_mm256_mul_ps(x, log2ef), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    __m256 z = _mm256_fnmadd_ps(fx, ln2_hi, x);
    z = _mm256_fnmadd_ps(fx, ln2_lo, z);

    __m256 y = _mm256_set1_ps(1.9875691500e-4f);
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(1.3981999507e-3f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(8.3334519073e-3f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(4.1665795894e-2f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(1.6666665459e-1f));
    y = _mm256_fmadd_ps(y, z, _mm256_set1_ps(5.0000001201e-1f));
    y = _mm256_fmadd_ps(y, _mm256_mul_ps(z, z), z);
    y = _mm256_add_ps(y, _mm256_set1_ps(1.0f));

    __m256i emm0 = _mm256_cvttps_epi32(fx);
    emm0 = _mm256_add_epi32(emm0, _mm256_set1_epi32(127));
    emm0 = _mm256_slli_epi32(emm0, 23);
    return _mm256_mul_ps(y, _mm256_castsi256_ps(emm0));
}

inline __m256 avx2_sigmoid_ps(__m256 x) {
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 neg_x = _mm256_sub_ps(_mm256_setzero_ps(), x);
    return _mm256_div_ps(one, _mm256_add_ps(one, avx2_exp_ps(neg_x)));
}

inline __m256 avx2_silu_ps(__m256 x) {
    return _mm256_mul_ps(x, avx2_sigmoid_ps(x));
}

inline __m256 avx2_tanh_ps(__m256 x) {
    const __m256 two = _mm256_set1_ps(2.0f);
    const __m256 one = _mm256_set1_ps(1.0f);
    __m256 s = avx2_sigmoid_ps(_mm256_mul_ps(two, x));
    return _mm256_fmsub_ps(two, s, one);
}

inline void avx2_silu(const float* in, float* out, uint32_t n) {
    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 x = _mm256_loadu_ps(in + i);
        _mm256_storeu_ps(out + i, avx2_silu_ps(x));
    }
    for (; i < n; ++i) {
        out[i] = bench_model::ref_silu(in[i]);
    }
}

inline void avx2_sigmoid(const float* in, float* out, uint32_t n) {
    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 x = _mm256_loadu_ps(in + i);
        _mm256_storeu_ps(out + i, avx2_sigmoid_ps(x));
    }
    for (; i < n; ++i) {
        out[i] = bench_model::ref_sigmoid(in[i]);
    }
}

inline void avx2_tanh(const float* in, float* out, uint32_t n) {
    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 x = _mm256_loadu_ps(in + i);
        _mm256_storeu_ps(out + i, avx2_tanh_ps(x));
    }
    for (; i < n; ++i) {
        out[i] = bench_model::ref_tanh(in[i]);
    }
}

inline void avx2_sgemm_bias_act(
    const float* __restrict A,
    const float* __restrict B,
    const float* __restrict bias,
    float* __restrict C,
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
        float* c0 = C + (m + 0) * N;
        float* c1 = C + (m + 1) * N;
        float* c2 = C + (m + 2) * N;
        float* c3 = C + (m + 3) * N;

        uint32_t n = 0;
        for (; n + 24 <= N; n += 24) {
            __m256 b0_init = bias ? _mm256_loadu_ps(bias + n + 0) : _mm256_setzero_ps();
            __m256 b1_init = bias ? _mm256_loadu_ps(bias + n + 8) : _mm256_setzero_ps();
            __m256 b2_init = bias ? _mm256_loadu_ps(bias + n + 16) : _mm256_setzero_ps();

            __m256 acc00 = b0_init, acc01 = b1_init, acc02 = b2_init;
            __m256 acc10 = b0_init, acc11 = b1_init, acc12 = b2_init;
            __m256 acc20 = b0_init, acc21 = b1_init, acc22 = b2_init;
            __m256 acc30 = b0_init, acc31 = b1_init, acc32 = b2_init;

            const float* b_ptr = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m256 bv0 = _mm256_loadu_ps(b_ptr + 0);
                __m256 bv1 = _mm256_loadu_ps(b_ptr + 8);
                __m256 bv2 = _mm256_loadu_ps(b_ptr + 16);

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

                b_ptr += N;
            }
            if (silu) {
                acc00 = avx2_silu_ps(acc00); acc01 = avx2_silu_ps(acc01); acc02 = avx2_silu_ps(acc02);
                acc10 = avx2_silu_ps(acc10); acc11 = avx2_silu_ps(acc11); acc12 = avx2_silu_ps(acc12);
                acc20 = avx2_silu_ps(acc20); acc21 = avx2_silu_ps(acc21); acc22 = avx2_silu_ps(acc22);
                acc30 = avx2_silu_ps(acc30); acc31 = avx2_silu_ps(acc31); acc32 = avx2_silu_ps(acc32);
            }
            _mm256_storeu_ps(c0 + n + 0, acc00); _mm256_storeu_ps(c0 + n + 8, acc01); _mm256_storeu_ps(c0 + n + 16, acc02);
            _mm256_storeu_ps(c1 + n + 0, acc10); _mm256_storeu_ps(c1 + n + 8, acc11); _mm256_storeu_ps(c1 + n + 16, acc12);
            _mm256_storeu_ps(c2 + n + 0, acc20); _mm256_storeu_ps(c2 + n + 8, acc21); _mm256_storeu_ps(c2 + n + 16, acc22);
            _mm256_storeu_ps(c3 + n + 0, acc30); _mm256_storeu_ps(c3 + n + 8, acc31); _mm256_storeu_ps(c3 + n + 16, acc32);
        }

        for (; n + 8 <= N; n += 8) {
            __m256 b0_init = bias ? _mm256_loadu_ps(bias + n) : _mm256_setzero_ps();
            __m256 acc0 = b0_init, acc1 = b0_init, acc2 = b0_init, acc3 = b0_init;
            const float* b_ptr = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m256 bv = _mm256_loadu_ps(b_ptr);
                acc0 = _mm256_fmadd_ps(_mm256_set1_ps(a0[k]), bv, acc0);
                acc1 = _mm256_fmadd_ps(_mm256_set1_ps(a1[k]), bv, acc1);
                acc2 = _mm256_fmadd_ps(_mm256_set1_ps(a2[k]), bv, acc2);
                acc3 = _mm256_fmadd_ps(_mm256_set1_ps(a3[k]), bv, acc3);
                b_ptr += N;
            }
            if (silu) {
                acc0 = avx2_silu_ps(acc0); acc1 = avx2_silu_ps(acc1);
                acc2 = avx2_silu_ps(acc2); acc3 = avx2_silu_ps(acc3);
            }
            _mm256_storeu_ps(c0 + n, acc0); _mm256_storeu_ps(c1 + n, acc1);
            _mm256_storeu_ps(c2 + n, acc2); _mm256_storeu_ps(c3 + n, acc3);
        }

        for (; n < N; ++n) {
            float bval = bias ? bias[n] : 0.0f;
            float s0 = bval, s1 = bval, s2 = bval, s3 = bval;
            for (uint32_t k = 0; k < K; ++k) {
                float bk = B[k * N + n];
                s0 += a0[k] * bk;
                s1 += a1[k] * bk;
                s2 += a2[k] * bk;
                s3 += a3[k] * bk;
            }
            if (silu) {
                s0 = bench_model::ref_silu(s0); s1 = bench_model::ref_silu(s1);
                s2 = bench_model::ref_silu(s2); s3 = bench_model::ref_silu(s3);
            }
            c0[n] = s0; c1[n] = s1; c2[n] = s2; c3[n] = s3;
        }
    }

    for (; m < M; ++m) {
        const float* a0 = A + m * K;
        float* c0 = C + m * N;
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

            const float* b_ptr = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m256 av = _mm256_set1_ps(a0[k]);
                acc0 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 0), acc0);
                acc1 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 8), acc1);
                acc2 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 16), acc2);
                acc3 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 24), acc3);
                acc4 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 32), acc4);
                acc5 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 40), acc5);
                acc6 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 48), acc6);
                acc7 = _mm256_fmadd_ps(av, _mm256_loadu_ps(b_ptr + 56), acc7);
                b_ptr += N;
            }
            if (silu) {
                acc0 = avx2_silu_ps(acc0); acc1 = avx2_silu_ps(acc1);
                acc2 = avx2_silu_ps(acc2); acc3 = avx2_silu_ps(acc3);
                acc4 = avx2_silu_ps(acc4); acc5 = avx2_silu_ps(acc5);
                acc6 = avx2_silu_ps(acc6); acc7 = avx2_silu_ps(acc7);
            }
            _mm256_storeu_ps(c0 + n + 0, acc0); _mm256_storeu_ps(c0 + n + 8, acc1);
            _mm256_storeu_ps(c0 + n + 16, acc2); _mm256_storeu_ps(c0 + n + 24, acc3);
            _mm256_storeu_ps(c0 + n + 32, acc4); _mm256_storeu_ps(c0 + n + 40, acc5);
            _mm256_storeu_ps(c0 + n + 48, acc6); _mm256_storeu_ps(c0 + n + 56, acc7);
        }
        for (; n + 8 <= N; n += 8) {
            __m256 acc = bias ? _mm256_loadu_ps(bias + n) : _mm256_setzero_ps();
            const float* b_ptr = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                acc = _mm256_fmadd_ps(_mm256_set1_ps(a0[k]), _mm256_loadu_ps(b_ptr), acc);
                b_ptr += N;
            }
            if (silu) {
                acc = avx2_silu_ps(acc);
            }
            _mm256_storeu_ps(c0 + n, acc);
        }
        for (; n < N; ++n) {
            float acc = bias ? bias[n] : 0.0f;
            for (uint32_t k = 0; k < K; ++k) {
                acc += a0[k] * B[k * N + n];
            }
            if (silu) acc = bench_model::ref_silu(acc);
            c0[n] = acc;
        }
    }
}

struct CpuWorkspace {
    std::vector<float> a1;
    std::vector<float> a2;
    std::vector<float> a3;
    std::vector<float> gx;
    std::vector<float> gh;

    void ensure(uint32_t batch) {
        using C = bench_model::ModelConfig;
        if (a1.size() < batch * C::H1) a1.resize(batch * C::H1);
        if (a2.size() < batch * C::H2) a2.resize(batch * C::H2);
        if (a3.size() < batch * C::H3) a3.resize(batch * C::H3);
        if (gx.size() < batch * 3 * C::GRU_H) gx.resize(batch * 3 * C::GRU_H);
        if (gh.size() < batch * 3 * C::GRU_H) gh.resize(batch * 3 * C::GRU_H);
    }
};

inline void avx2_gru_step(
    const float* __restrict x,
    const float* __restrict h_prev,
    const float* __restrict w_ih,
    const float* __restrict b_ih,
    const float* __restrict w_hh,
    const float* __restrict b_hh,
    float* __restrict h_out,
    float* __restrict gx,
    float* __restrict gh,
    uint32_t batch,
    uint32_t in_dim,
    uint32_t hidden_dim
) {
    uint32_t H3 = 3 * hidden_dim;
    avx2_sgemm_bias_act(x, w_ih, b_ih, gx, batch, H3, in_dim, false);
    avx2_sgemm_bias_act(h_prev, w_hh, b_hh, gh, batch, H3, hidden_dim, false);

    const __m256 one = _mm256_set1_ps(1.0f);
    for (uint32_t b = 0; b < batch; ++b) {
        const float* gxb = gx + b * H3;
        const float* ghb = gh + b * H3;
        const float* hpb = h_prev + b * hidden_dim;
        float* hob = h_out + b * hidden_dim;

        uint32_t i = 0;
        for (; i + 8 <= hidden_dim; i += 8) {
            __m256 xz = _mm256_loadu_ps(gxb + i);
            __m256 xr = _mm256_loadu_ps(gxb + hidden_dim + i);
            __m256 xn = _mm256_loadu_ps(gxb + 2 * hidden_dim + i);

            __m256 hz = _mm256_loadu_ps(ghb + i);
            __m256 hr = _mm256_loadu_ps(ghb + hidden_dim + i);
            __m256 hn = _mm256_loadu_ps(ghb + 2 * hidden_dim + i);

            __m256 z = avx2_sigmoid_ps(_mm256_add_ps(xz, hz));
            __m256 r = avx2_sigmoid_ps(_mm256_add_ps(xr, hr));
            __m256 n_gate = avx2_tanh_ps(_mm256_fmadd_ps(r, hn, xn));

            __m256 hp = _mm256_loadu_ps(hpb + i);
            __m256 one_minus_z = _mm256_sub_ps(one, z);
            __m256 h_new = _mm256_fmadd_ps(one_minus_z, n_gate, _mm256_mul_ps(z, hp));
            _mm256_storeu_ps(hob + i, h_new);
        }
        for (; i < hidden_dim; ++i) {
            float z = bench_model::ref_sigmoid(gxb[i] + ghb[i]);
            float r = bench_model::ref_sigmoid(gxb[hidden_dim + i] + ghb[hidden_dim + i]);
            float n_gate = bench_model::ref_tanh(gxb[2 * hidden_dim + i] + r * ghb[2 * hidden_dim + i]);
            hob[i] = (1.0f - z) * n_gate + z * hpb[i];
        }
    }
}

inline void avx2_forward(
    const bench_model::ModelWeights& w,
    const float* x,
    const float* h_init,
    float* out,
    float* h_next,
    CpuWorkspace& ws,
    uint32_t batch
) {
    using C = bench_model::ModelConfig;
    const float* p = w.params.data();
    avx2_sgemm_bias_act(x, p + w.off_w1, p + w.off_b1, ws.a1.data(), batch, C::H1, C::IN_DIM, true);
    avx2_sgemm_bias_act(ws.a1.data(), p + w.off_w2, p + w.off_b2, ws.a2.data(), batch, C::H2, C::H1, true);
    avx2_sgemm_bias_act(ws.a2.data(), p + w.off_w3, p + w.off_b3, ws.a3.data(), batch, C::H3, C::H2, true);
    avx2_gru_step(
        ws.a3.data(), h_init,
        p + w.off_w_ih, p + w.off_b_ih,
        p + w.off_w_hh, p + w.off_b_hh,
        h_next, ws.gx.data(), ws.gh.data(),
        batch, C::H3, C::GRU_H
    );
    avx2_sgemm_bias_act(h_next, p + w.off_w_out, p + w.off_b_out, out, batch, C::OUT_DIM, C::GRU_H, false);
}

inline void avx2_adamw_step(
    float* __restrict params,
    const float* __restrict grads,
    float* __restrict m,
    float* __restrict v,
    uint32_t n,
    float lr,
    float beta1,
    float beta2,
    float eps,
    float weight_decay,
    float inv_bc1,
    float inv_bc2
) {
    const __m256 vb1 = _mm256_set1_ps(beta1);
    const __m256 v1mb1 = _mm256_set1_ps(1.0f - beta1);
    const __m256 vb2 = _mm256_set1_ps(beta2);
    const __m256 v1mb2 = _mm256_set1_ps(1.0f - beta2);
    const __m256 vdecay = _mm256_set1_ps(1.0f - lr * weight_decay);
    const __m256 vlr = _mm256_set1_ps(lr);
    const __m256 veps = _mm256_set1_ps(eps);
    const __m256 vbc1 = _mm256_set1_ps(inv_bc1);
    const __m256 vbc2 = _mm256_set1_ps(inv_bc2);

    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 g = _mm256_loadu_ps(grads + i);
        __m256 mi = _mm256_loadu_ps(m + i);
        __m256 vi = _mm256_loadu_ps(v + i);
        __m256 pi = _mm256_loadu_ps(params + i);

        mi = _mm256_fmadd_ps(vb1, mi, _mm256_mul_ps(v1mb1, g));
        vi = _mm256_fmadd_ps(vb2, vi, _mm256_mul_ps(v1mb2, _mm256_mul_ps(g, g)));

        _mm256_storeu_ps(m + i, mi);
        _mm256_storeu_ps(v + i, vi);

        __m256 m_hat = _mm256_mul_ps(mi, vbc1);
        __m256 v_hat = _mm256_mul_ps(vi, vbc2);
        __m256 denom = _mm256_add_ps(_mm256_sqrt_ps(v_hat), veps);
        __m256 step = _mm256_mul_ps(vlr, _mm256_div_ps(m_hat, denom));
        pi = _mm256_fmsub_ps(pi, vdecay, step);
        _mm256_storeu_ps(params + i, pi);
    }
    if (i < n) {
        bench_model::ref_adamw_step(params + i, grads + i, m + i, v + i, n - i, lr, beta1, beta2, eps, weight_decay, inv_bc1, inv_bc2);
    }
}

inline void sse2_sgemm_bias_act(
    const float* __restrict A,
    const float* __restrict B,
    const float* __restrict bias,
    float* __restrict C,
    uint32_t M,
    uint32_t N,
    uint32_t K,
    bool silu
) {
    for (uint32_t m = 0; m < M; ++m) {
        const float* a_row = A + m * K;
        float* c_row = C + m * N;
        uint32_t n = 0;
        for (; n + 4 <= N; n += 4) {
            __m128 acc = bias ? _mm_loadu_ps(bias + n) : _mm_setzero_ps();
            const float* b_ptr = B + n;
            for (uint32_t k = 0; k < K; ++k) {
                __m128 av = _mm_set1_ps(a_row[k]);
                __m128 bv = _mm_loadu_ps(b_ptr);
                acc = _mm_add_ps(acc, _mm_mul_ps(av, bv));
                b_ptr += N;
            }
            if (silu) {
                alignas(16) float tmp[4];
                _mm_store_ps(tmp, acc);
                for (int i = 0; i < 4; ++i) tmp[i] = bench_model::ref_silu(tmp[i]);
                acc = _mm_load_ps(tmp);
            }
            _mm_storeu_ps(c_row + n, acc);
        }
        for (; n < N; ++n) {
            float acc = bias ? bias[n] : 0.0f;
            for (uint32_t k = 0; k < K; ++k) acc += a_row[k] * B[k * N + n];
            if (silu) acc = bench_model::ref_silu(acc);
            c_row[n] = acc;
        }
    }
}

inline void sse2_forward(
    const bench_model::ModelWeights& w,
    const float* x,
    const float* h_init,
    float* out,
    float* h_next,
    CpuWorkspace& ws,
    uint32_t batch
) {
    using C = bench_model::ModelConfig;
    const float* p = w.params.data();
    sse2_sgemm_bias_act(x, p + w.off_w1, p + w.off_b1, ws.a1.data(), batch, C::H1, C::IN_DIM, true);
    sse2_sgemm_bias_act(ws.a1.data(), p + w.off_w2, p + w.off_b2, ws.a2.data(), batch, C::H2, C::H1, true);
    sse2_sgemm_bias_act(ws.a2.data(), p + w.off_w3, p + w.off_b3, ws.a3.data(), batch, C::H3, C::H2, true);
    uint32_t H3 = 3 * C::GRU_H;
    sse2_sgemm_bias_act(ws.a3.data(), p + w.off_w_ih, p + w.off_b_ih, ws.gx.data(), batch, H3, C::H3, false);
    sse2_sgemm_bias_act(h_init, p + w.off_w_hh, p + w.off_b_hh, ws.gh.data(), batch, H3, C::GRU_H, false);
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t i = 0; i < C::GRU_H; ++i) {
            float z = bench_model::ref_sigmoid(ws.gx[b * H3 + i] + ws.gh[b * H3 + i]);
            float r = bench_model::ref_sigmoid(ws.gx[b * H3 + C::GRU_H + i] + ws.gh[b * H3 + C::GRU_H + i]);
            float n_gate = bench_model::ref_tanh(ws.gx[b * H3 + 2 * C::GRU_H + i] + r * ws.gh[b * H3 + 2 * C::GRU_H + i]);
            h_next[b * C::GRU_H + i] = (1.0f - z) * n_gate + z * h_init[b * C::GRU_H + i];
        }
    }
    sse2_sgemm_bias_act(h_next, p + w.off_w_out, p + w.off_b_out, out, batch, C::OUT_DIM, C::GRU_H, false);
}

inline void forward_dispatch(
    const bench_model::ModelWeights& w,
    const float* x,
    const float* h_init,
    float* out,
    float* h_next,
    CpuWorkspace& ws,
    uint32_t batch,
    bool force_sse2 = false
) {
    if (!force_sse2 && is_avx2_fma_supported()) {
        avx2_forward(w, x, h_init, out, h_next, ws, batch);
    } else {
        sse2_forward(w, x, h_init, out, h_next, ws, batch);
    }
}

}
