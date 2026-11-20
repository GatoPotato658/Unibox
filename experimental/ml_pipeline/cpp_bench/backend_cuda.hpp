#pragma once

#include "common_model.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bench_cuda {

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

static const char* kPtxSource = R"ptx(
.version 6.0
.target sm_50
.address_size 64

.visible .entry sgemm_bias_act(
    .param .u64 p_A,
    .param .u64 p_B,
    .param .u64 p_bias,
    .param .u64 p_C,
    .param .u32 p_M,
    .param .u32 p_N,
    .param .u32 p_K,
    .param .u32 p_silu
)
{
    .reg .pred %p<5>;
    .reg .u32 %r<16>;
    .reg .u64 %rd<16>;
    .reg .f32 %f<12>;

    mov.u32 %r1, %ctaid.x;
    mov.u32 %r2, %ntid.x;
    mov.u32 %r3, %tid.x;
    mad.lo.u32 %r4, %r1, %r2, %r3;

    mov.u32 %r5, %ctaid.y;
    mov.u32 %r6, %ntid.y;
    mov.u32 %r7, %tid.y;
    mad.lo.u32 %r8, %r5, %r6, %r7;

    ld.param.u32 %r9, [p_M];
    ld.param.u32 %r10, [p_N];
    ld.param.u32 %r11, [p_K];

    setp.ge.u32 %p1, %r8, %r9;
    setp.ge.u32 %p2, %r4, %r10;
    or.pred %p3, %p1, %p2;
    @%p3 bra DONE;

    ld.param.u64 %rd1, [p_A];
    ld.param.u64 %rd2, [p_B];
    ld.param.u64 %rd3, [p_bias];
    ld.param.u64 %rd4, [p_C];

    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd4, %rd4;

    mov.f32 %f1, 0f00000000;
    setp.eq.u64 %p4, %rd3, 0;
    @%p4 bra SKIP_BIAS;
    cvta.to.global.u64 %rd3, %rd3;
    mul.wide.u32 %rd5, %r4, 4;
    add.u64 %rd6, %rd3, %rd5;
    ld.global.f32 %f1, [%rd6];
SKIP_BIAS:

    mul.lo.u32 %r12, %r8, %r11;
    mul.wide.u32 %rd7, %r12, 4;
    add.u64 %rd8, %rd1, %rd7;

    mul.wide.u32 %rd9, %r4, 4;
    add.u64 %rd10, %rd2, %rd9;
    mul.wide.u32 %rd11, %r10, 4;

    mov.u32 %r13, 0;
    setp.eq.u32 %p4, %r11, 0;
    @%p4 bra K_DONE;
K_LOOP:
    ld.global.f32 %f2, [%rd8];
    ld.global.f32 %f3, [%rd10];
    fma.rn.f32 %f1, %f2, %f3, %f1;
    add.u64 %rd8, %rd8, 4;
    add.u64 %rd10, %rd10, %rd11;
    add.u32 %r13, %r13, 1;
    setp.lt.u32 %p4, %r13, %r11;
    @%p4 bra K_LOOP;
K_DONE:

    ld.param.u32 %r14, [p_silu];
    setp.eq.u32 %p4, %r14, 0;
    @%p4 bra STORE_C;

    neg.f32 %f4, %f1;
    mul.f32 %f5, %f4, 0f3FB8AA3B;
    ex2.approx.f32 %f6, %f5;
    add.f32 %f7, %f6, 0f3F800000;
    div.rn.f32 %f8, 0f3F800000, %f7;
    mul.f32 %f1, %f1, %f8;

STORE_C:
    mad.lo.u32 %r15, %r8, %r10, %r4;
    mul.wide.u32 %rd12, %r15, 4;
    add.u64 %rd13, %rd4, %rd12;
    st.global.f32 [%rd13], %f1;
DONE:
    ret;
}

.visible .entry elem_silu(
    .param .u64 p_X,
    .param .u64 p_Y,
    .param .u32 p_N
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<6>;
    .reg .u64 %rd<6>;
    .reg .f32 %f<8>;

    mov.u32 %r1, %ctaid.x;
    mov.u32 %r2, %ntid.x;
    mov.u32 %r3, %tid.x;
    mad.lo.u32 %r4, %r1, %r2, %r3;
    ld.param.u32 %r5, [p_N];
    setp.ge.u32 %p1, %r4, %r5;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_X];
    ld.param.u64 %rd2, [p_Y];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    mul.wide.u32 %rd3, %r4, 4;
    add.u64 %rd4, %rd1, %rd3;
    add.u64 %rd5, %rd2, %rd3;
    ld.global.f32 %f1, [%rd4];
    neg.f32 %f2, %f1;
    mul.f32 %f3, %f2, 0f3FB8AA3B;
    ex2.approx.f32 %f4, %f3;
    add.f32 %f5, %f4, 0f3F800000;
    div.rn.f32 %f6, 0f3F800000, %f5;
    mul.f32 %f7, %f1, %f6;
    st.global.f32 [%rd5], %f7;
DONE:
    ret;
}

.visible .entry elem_sigmoid(
    .param .u64 p_X,
    .param .u64 p_Y,
    .param .u32 p_N
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<6>;
    .reg .u64 %rd<6>;
    .reg .f32 %f<7>;

    mov.u32 %r1, %ctaid.x;
    mov.u32 %r2, %ntid.x;
    mov.u32 %r3, %tid.x;
    mad.lo.u32 %r4, %r1, %r2, %r3;
    ld.param.u32 %r5, [p_N];
    setp.ge.u32 %p1, %r4, %r5;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_X];
    ld.param.u64 %rd2, [p_Y];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    mul.wide.u32 %rd3, %r4, 4;
    add.u64 %rd4, %rd1, %rd3;
    add.u64 %rd5, %rd2, %rd3;
    ld.global.f32 %f1, [%rd4];
    neg.f32 %f2, %f1;
    mul.f32 %f3, %f2, 0f3FB8AA3B;
    ex2.approx.f32 %f4, %f3;
    add.f32 %f5, %f4, 0f3F800000;
    div.rn.f32 %f6, 0f3F800000, %f5;
    st.global.f32 [%rd5], %f6;
DONE:
    ret;
}

.visible .entry elem_tanh(
    .param .u64 p_X,
    .param .u64 p_Y,
    .param .u32 p_N
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<6>;
    .reg .u64 %rd<6>;
    .reg .f32 %f<9>;

    mov.u32 %r1, %ctaid.x;
    mov.u32 %r2, %ntid.x;
    mov.u32 %r3, %tid.x;
    mad.lo.u32 %r4, %r1, %r2, %r3;
    ld.param.u32 %r5, [p_N];
    setp.ge.u32 %p1, %r4, %r5;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_X];
    ld.param.u64 %rd2, [p_Y];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    mul.wide.u32 %rd3, %r4, 4;
    add.u64 %rd4, %rd1, %rd3;
    add.u64 %rd5, %rd2, %rd3;
    ld.global.f32 %f1, [%rd4];
    mul.f32 %f2, %f1, 0f40000000;
    neg.f32 %f3, %f2;
    mul.f32 %f4, %f3, 0f3FB8AA3B;
    ex2.approx.f32 %f5, %f4;
    add.f32 %f6, %f5, 0f3F800000;
    div.rn.f32 %f7, 0f3F800000, %f6;
    fma.rn.f32 %f8, %f7, 0f40000000, 0fBF800000;
    st.global.f32 [%rd5], %f8;
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

.visible .entry adamw_step(
    .param .u64 p_params,
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
    .param .f32 p_inv_bc2
)
{
    .reg .pred %p<2>;
    .reg .u32 %r<6>;
    .reg .u64 %rd<10>;
    .reg .f32 %f<18>;

    mov.u32 %r1, %ctaid.x;
    mov.u32 %r2, %ntid.x;
    mov.u32 %r3, %tid.x;
    mad.lo.u32 %r4, %r1, %r2, %r3;
    ld.param.u32 %r5, [p_N];
    setp.ge.u32 %p1, %r4, %r5;
    @%p1 bra DONE;

    ld.param.u64 %rd1, [p_params];
    ld.param.u64 %rd2, [p_grads];
    ld.param.u64 %rd3, [p_m];
    ld.param.u64 %rd4, [p_v];
    cvta.to.global.u64 %rd1, %rd1;
    cvta.to.global.u64 %rd2, %rd2;
    cvta.to.global.u64 %rd3, %rd3;
    cvta.to.global.u64 %rd4, %rd4;

    mul.wide.u32 %rd5, %r4, 4;
    add.u64 %rd6, %rd1, %rd5;
    add.u64 %rd7, %rd2, %rd5;
    add.u64 %rd8, %rd3, %rd5;
    add.u64 %rd9, %rd4, %rd5;

    ld.global.f32 %f1, [%rd6];
    ld.global.f32 %f2, [%rd7];
    ld.global.f32 %f3, [%rd8];
    ld.global.f32 %f4, [%rd9];

    ld.param.f32 %f5, [p_lr];
    ld.param.f32 %f6, [p_b1];
    ld.param.f32 %f7, [p_b2];
    ld.param.f32 %f8, [p_eps];
    ld.param.f32 %f9, [p_decay];
    ld.param.f32 %f10, [p_inv_bc1];
    ld.param.f32 %f11, [p_inv_bc2];

    sub.f32 %f12, 0f3F800000, %f6;
    mul.f32 %f13, %f12, %f2;
    fma.rn.f32 %f3, %f6, %f3, %f13;

    sub.f32 %f12, 0f3F800000, %f7;
    mul.f32 %f14, %f2, %f2;
    mul.f32 %f13, %f12, %f14;
    fma.rn.f32 %f4, %f7, %f4, %f13;

    st.global.f32 [%rd8], %f3;
    st.global.f32 [%rd9], %f4;

    mul.f32 %f15, %f3, %f10;
    mul.f32 %f16, %f4, %f11;
    sqrt.rn.f32 %f16, %f16;
    add.f32 %f16, %f16, %f8;
    div.rn.f32 %f17, %f15, %f16;
    mul.f32 %f17, %f5, %f17;
    neg.f32 %f17, %f17;
    fma.rn.f32 %f1, %f1, %f9, %f17;
    st.global.f32 [%rd6], %f1;
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
    CUresult(__stdcall* cuCtxSynchronize)() = nullptr;
    CUresult(__stdcall* cuMemGetInfo_v2)(size_t*, size_t*) = nullptr;
    CUresult(__stdcall* cuMemAlloc_v2)(CUdeviceptr*, size_t) = nullptr;
    CUresult(__stdcall* cuMemFree_v2)(CUdeviceptr) = nullptr;
    CUresult(__stdcall* cuMemcpyHtoD_v2)(CUdeviceptr, const void*, size_t) = nullptr;
    CUresult(__stdcall* cuMemcpyDtoH_v2)(void*, CUdeviceptr, size_t) = nullptr;
    CUresult(__stdcall* cuMemcpyHtoDAsync_v2)(CUdeviceptr, const void*, size_t, CUstream) = nullptr;
    CUresult(__stdcall* cuMemcpyDtoHAsync_v2)(void*, CUdeviceptr, size_t, CUstream) = nullptr;
    CUresult(__stdcall* cuModuleLoadData)(CUmodule*, const void*) = nullptr;
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
               gp(cuCtxSynchronize, "cuCtxSynchronize") &&
               gp(cuMemGetInfo_v2, "cuMemGetInfo_v2") &&
               gp(cuMemAlloc_v2, "cuMemAlloc_v2") &&
               gp(cuMemFree_v2, "cuMemFree_v2") &&
               gp(cuMemcpyHtoD_v2, "cuMemcpyHtoD_v2") &&
               gp(cuMemcpyDtoH_v2, "cuMemcpyDtoH_v2") &&
               gp(cuMemcpyHtoDAsync_v2, "cuMemcpyHtoDAsync_v2") &&
               gp(cuMemcpyDtoHAsync_v2, "cuMemcpyDtoHAsync_v2") &&
               gp(cuModuleLoadData, "cuModuleLoadData") &&
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
    CUfunction fn_sgemm = nullptr;
    CUfunction fn_silu = nullptr;
    CUfunction fn_sigmoid = nullptr;
    CUfunction fn_tanh = nullptr;
    CUfunction fn_gru = nullptr;
    CUfunction fn_adamw = nullptr;
    CudaDeviceInfo info{};

    CUdeviceptr d_params = 0;
    CUdeviceptr d_grads = 0;
    CUdeviceptr d_m = 0;
    CUdeviceptr d_v = 0;

    CUdeviceptr d_x = 0;
    CUdeviceptr d_h0 = 0;
    CUdeviceptr d_a1 = 0;
    CUdeviceptr d_a2 = 0;
    CUdeviceptr d_a3 = 0;
    CUdeviceptr d_gx = 0;
    CUdeviceptr d_gh = 0;
    CUdeviceptr d_h1 = 0;
    CUdeviceptr d_out = 0;
    uint32_t max_batch = 0;
    size_t allocated_bytes = 0;

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
        r = api.cuModuleLoadData(&mod, kPtxSource);
        if (r != CUDA_SUCCESS) {
            std::printf("cuModuleLoadData failed: %s\n", api.err_str(r).c_str());
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
        if (!gf(fn_sgemm, "sgemm_bias_act") ||
            !gf(fn_silu, "elem_silu") ||
            !gf(fn_sigmoid, "elem_sigmoid") ||
            !gf(fn_tanh, "elem_tanh") ||
            !gf(fn_gru, "gru_gates") ||
            !gf(fn_adamw, "adamw_step")) {
            return false;
        }
        r = api.cuStreamCreate(&stream, 0);
        if (r != CUDA_SUCCESS) return false;
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

    bool allocate_model(const bench_model::ModelWeights& w, uint32_t batch_cap) {
        using C = bench_model::ModelConfig;
        max_batch = batch_cap;
        size_t pbytes = C::TOTAL_PARAMS * sizeof(float);
        d_params = alloc_bytes(pbytes);
        d_grads = alloc_bytes(pbytes);
        d_m = alloc_bytes(pbytes);
        d_v = alloc_bytes(pbytes);
        d_x = alloc_bytes(batch_cap * C::IN_DIM * sizeof(float));
        d_h0 = alloc_bytes(batch_cap * C::GRU_H * sizeof(float));
        d_a1 = alloc_bytes(batch_cap * C::H1 * sizeof(float));
        d_a2 = alloc_bytes(batch_cap * C::H2 * sizeof(float));
        d_a3 = alloc_bytes(batch_cap * C::H3 * sizeof(float));
        d_gx = alloc_bytes(batch_cap * 3 * C::GRU_H * sizeof(float));
        d_gh = alloc_bytes(batch_cap * 3 * C::GRU_H * sizeof(float));
        d_h1 = alloc_bytes(batch_cap * C::GRU_H * sizeof(float));
        d_out = alloc_bytes(batch_cap * C::OUT_DIM * sizeof(float));

        if (!d_params || !d_grads || !d_m || !d_v || !d_x || !d_h0 || !d_a1 || !d_a2 || !d_a3 || !d_gx || !d_gh || !d_h1 || !d_out) {
            return false;
        }
        if (api.cuMemcpyHtoD_v2(d_params, w.params.data(), pbytes) != CUDA_SUCCESS) return false;
        return true;
    }

    void launch_sgemm(CUdeviceptr A, CUdeviceptr B, CUdeviceptr bias, CUdeviceptr C, uint32_t M, uint32_t N, uint32_t K, uint32_t silu) {
        uint32_t bx = (M == 1) ? 64u : 32u;
        uint32_t by = (M == 1) ? 1u : 8u;
        uint32_t gx = (N + bx - 1) / bx;
        uint32_t gy = (M + by - 1) / by;
        void* params[] = { &A, &B, &bias, &C, &M, &N, &K, &silu };
        api.cuLaunchKernel(fn_sgemm, gx, gy, 1, bx, by, 1, 0, stream, params, nullptr);
    }

    void launch_elem(CUfunction fn, CUdeviceptr X, CUdeviceptr Y, uint32_t N) {
        uint32_t bx = 256;
        uint32_t gx = (N + bx - 1) / bx;
        void* params[] = { &X, &Y, &N };
        api.cuLaunchKernel(fn, gx, 1, 1, bx, 1, 1, 0, stream, params, nullptr);
    }

    void launch_gru_gates(CUdeviceptr gx_buf, CUdeviceptr gh_buf, CUdeviceptr hp, CUdeviceptr ho, uint32_t batch, uint32_t hidden) {
        uint32_t total = batch * hidden;
        uint32_t bx = 128;
        uint32_t gx = (total + bx - 1) / bx;
        void* params[] = { &gx_buf, &gh_buf, &hp, &ho, &batch, &hidden };
        api.cuLaunchKernel(fn_gru, gx, 1, 1, bx, 1, 1, 0, stream, params, nullptr);
    }

    void forward_async(const bench_model::ModelWeights& w, uint32_t batch) {
        using C = bench_model::ModelConfig;
        auto ptr_off = [](CUdeviceptr base, size_t floats) -> CUdeviceptr {
            return base + floats * sizeof(float);
        };
        launch_sgemm(d_x, ptr_off(d_params, w.off_w1), ptr_off(d_params, w.off_b1), d_a1, batch, C::H1, C::IN_DIM, 1);
        launch_sgemm(d_a1, ptr_off(d_params, w.off_w2), ptr_off(d_params, w.off_b2), d_a2, batch, C::H2, C::H1, 1);
        launch_sgemm(d_a2, ptr_off(d_params, w.off_w3), ptr_off(d_params, w.off_b3), d_a3, batch, C::H3, C::H2, 1);
        launch_sgemm(d_a3, ptr_off(d_params, w.off_w_ih), ptr_off(d_params, w.off_b_ih), d_gx, batch, 3 * C::GRU_H, C::H3, 0);
        launch_sgemm(d_h0, ptr_off(d_params, w.off_w_hh), ptr_off(d_params, w.off_b_hh), d_gh, batch, 3 * C::GRU_H, C::GRU_H, 0);
        launch_gru_gates(d_gx, d_gh, d_h0, d_h1, batch, C::GRU_H);
        launch_sgemm(d_h1, ptr_off(d_params, w.off_w_out), ptr_off(d_params, w.off_b_out), d_out, batch, C::OUT_DIM, C::GRU_H, 0);
    }

    void adamw_async(uint32_t n, float lr, float b1, float b2, float eps, float wd, float inv_bc1, float inv_bc2) {
        float decay = 1.0f - lr * wd;
        uint32_t bx = 256;
        uint32_t gx = (n + bx - 1) / bx;
        void* params[] = { &d_params, &d_grads, &d_m, &d_v, &n, &lr, &b1, &b2, &eps, &decay, &inv_bc1, &inv_bc2 };
        api.cuLaunchKernel(fn_adamw, gx, 1, 1, bx, 1, 1, 0, stream, params, nullptr);
    }

    void destroy() {
        auto fr = [&](CUdeviceptr& p) { if (p) { api.cuMemFree_v2(p); p = 0; } };
        fr(d_params); fr(d_grads); fr(d_m); fr(d_v);
        fr(d_x); fr(d_h0); fr(d_a1); fr(d_a2); fr(d_a3);
        fr(d_gx); fr(d_gh); fr(d_h1); fr(d_out);
        if (stream) { api.cuStreamDestroy_v2(stream); stream = nullptr; }
        if (mod) { api.cuModuleUnload(mod); mod = nullptr; }
        if (ctx) { api.cuCtxDestroy_v2(ctx); ctx = nullptr; }
        if (api.dll) { FreeLibrary(api.dll); api.dll = nullptr; }
    }
};

}
