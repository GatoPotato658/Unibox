#pragma once

#include "strafenet_model.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

static const char* kStrafeNetPtx = R"ptx(
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
)ptx";

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

    std::string err_str(CUresult r) const {
        const char* n = nullptr;
        const char* s = nullptr;
        if (cuGetErrorName) cuGetErrorName(r, &n);
        if (cuGetErrorString) cuGetErrorString(r, &s);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s (%d): %s", n ? n : "?", r, s ? s : "?");
        return std::string(buf);
    }
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
        if (api.cuInit(0) != CUDA_SUCCESS) return false;
        int count = 0;
        if (api.cuDeviceGetCount(&count) != CUDA_SUCCESS || count <= ordinal) return false;
        if (api.cuDeviceGet(&dev, ordinal) != CUDA_SUCCESS) return false;

        char name_buf[256] = {};
        api.cuDeviceGetName(name_buf, sizeof(name_buf), dev);
        info.ordinal = ordinal;
        info.name = name_buf;
        api.cuDeviceTotalMem_v2(&info.total_vram_bytes, dev);
        api.cuDeviceGetAttribute(&info.sm_count, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, dev);
        api.cuDeviceGetAttribute(&info.cc_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
        api.cuDeviceGetAttribute(&info.cc_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);

        CUresult r = api.cuCtxCreate_v2(&ctx, 0, dev);
        if (r != CUDA_SUCCESS) {
            std::printf("cuCtxCreate_v2 failed: %s\n", api.err_str(r).c_str());
            return false;
        }
        char err_log[4096] = {};
        int opts[2] = { 5, 6 };
        void* opt_vals[2] = { err_log, reinterpret_cast<void*>(static_cast<uintptr_t>(sizeof(err_log))) };
        r = api.cuModuleLoadDataEx(&mod, kStrafeNetPtx, 2, opts, opt_vals);
        if (r != CUDA_SUCCESS) {
            std::printf("cuModuleLoadDataEx failed: %s\nPTX log:\n%s\n", api.err_str(r).c_str(), err_log);
            return false;
        }
        auto gf = [&](CUfunction& fn, const char* name) -> bool {
            CUresult gr = api.cuModuleGetFunction(&fn, mod, name);
            if (gr != CUDA_SUCCESS) {
                std::printf("cuModuleGetFunction(%s) failed: %s\n", name, api.err_str(gr).c_str());
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
