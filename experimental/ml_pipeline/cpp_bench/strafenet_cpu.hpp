#pragma once

#include "strafenet_model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <intrin.h>
#include <vector>

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
