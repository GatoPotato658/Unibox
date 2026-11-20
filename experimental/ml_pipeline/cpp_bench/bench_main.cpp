#include "common_model.hpp"
#include "backend_cpu.hpp"
#include "backend_cuda.hpp"
#include "backend_directml.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <vector>

namespace {

struct BenchStats {
    double mean_us = 0.0;
    double min_us = 0.0;
    double p50_us = 0.0;
    double p95_us = 0.0;
};

BenchStats compute_stats(std::vector<double>& samples) {
    BenchStats s{};
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    s.mean_us = sum / static_cast<double>(samples.size());
    s.min_us = samples.front();
    s.p50_us = samples[samples.size() / 2];
    s.p95_us = samples[(samples.size() * 95) / 100];
    return s;
}

template <typename Fn>
BenchStats bench_fn(int warmup, int iters, Fn&& fn) {
    for (int i = 0; i < warmup; ++i) fn();
    std::vector<double> samples(iters);
    for (int i = 0; i < iters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();
        samples[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    return compute_stats(samples);
}

void fill_deterministic(std::vector<float>& v, uint32_t seed, float scale) {
    uint32_t s = seed;
    for (size_t i = 0; i < v.size(); ++i) {
        s = s * 1664525u + 1013904223u;
        int32_t x = static_cast<int32_t>(s >> 8) - 8388608;
        v[i] = static_cast<float>(x) * (scale / 8388608.0f);
    }
}

void print_stats(const char* label, const BenchStats& st) {
    std::printf("  %-42s mean=%8.2f us  min=%8.2f us  p50=%8.2f us  p95=%8.2f us\n",
        label, st.mean_us, st.min_us, st.p50_us, st.p95_us);
}

bool test_dml_standalone_ops(bench_dml::DmlBackend& dml) {
    using namespace bench_dml;
    const uint32_t M = 4, K = 64, N = 128;
    std::vector<float> A(M * K), B(K * N), bias(N), C_ref(M * N), C_dml(M * N);
    fill_deterministic(A, 11, 1.0f);
    fill_deterministic(B, 22, 0.2f);
    fill_deterministic(bias, 33, 0.5f);
    bench_model::ref_sgemm_bias_act(A.data(), B.data(), bias.data(), C_ref.data(), M, N, K, false);

    DmlBuffer bA, bB, bBias, bC;
    auto mk = [&](uint64_t bytes, DmlBuffer& buf) {
        return dml.create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, buf);
    };
    if (!mk(M * K * 4, bA) || !mk(K * N * 4, bB) || !mk(N * 4, bBias) || !mk(M * N * 4, bC)) return false;
    if (!dml.upload_buffer(bA, A.data(), M * K * 4) ||
        !dml.upload_buffer(bB, B.data(), K * N * 4) ||
        !dml.upload_buffer(bBias, bias.data(), N * 4)) return false;

    UINT sA[4] = { 1, 1, M, K }, sB[4] = { 1, 1, K, N }, sBias[4] = { 1, 1, M, N }, stBias[4] = { 0, 0, 0, 1 }, sC[4] = { 1, 1, M, N };
    DML_BUFFER_TENSOR_DESC btA{ DML_TENSOR_DATA_TYPE_FLOAT32, DML_TENSOR_FLAG_NONE, 4, sA, nullptr, M * K * 4, 0 };
    DML_BUFFER_TENSOR_DESC btB{ DML_TENSOR_DATA_TYPE_FLOAT32, DML_TENSOR_FLAG_NONE, 4, sB, nullptr, K * N * 4, 0 };
    DML_BUFFER_TENSOR_DESC btBias{ DML_TENSOR_DATA_TYPE_FLOAT32, DML_TENSOR_FLAG_NONE, 4, sBias, stBias, N * 4, 0 };
    DML_BUFFER_TENSOR_DESC btC{ DML_TENSOR_DATA_TYPE_FLOAT32, DML_TENSOR_FLAG_NONE, 4, sC, nullptr, M * N * 4, 0 };
    DML_TENSOR_DESC tA{ DML_TENSOR_TYPE_BUFFER, &btA };
    DML_TENSOR_DESC tB{ DML_TENSOR_TYPE_BUFFER, &btB };
    DML_TENSOR_DESC tBias{ DML_TENSOR_TYPE_BUFFER, &btBias };
    DML_TENSOR_DESC tC{ DML_TENSOR_TYPE_BUFFER, &btC };

    DML_GEMM_OPERATOR_DESC gd{ &tA, &tB, &tBias, &tC, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
    DML_OPERATOR_DESC od{ DML_OPERATOR_GEMM, &gd };
    ComPtr<IDMLOperator> op;
    ComPtr<IDMLCompiledOperator> cop;
    if (FAILED(dml.dml_device->CreateOperator(&od, IID_PPV_ARGS(&op)))) return false;
    if (FAILED(dml.dml_device->CompileOperator(op.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&cop)))) return false;

    IDMLCompiledOperator* cops[] = { cop.Get() };
    ComPtr<IDMLOperatorInitializer> init;
    if (FAILED(dml.dml_device->CreateOperatorInitializer(1, cops, IID_PPV_ARGS(&init)))) return false;

    DML_BINDING_PROPERTIES ep = cop->GetBindingProperties();
    DML_BINDING_PROPERTIES ip = init->GetBindingProperties();
    UINT n_desc = std::max<UINT>({ ep.RequiredDescriptorCount, ip.RequiredDescriptorCount, 1 });
    D3D12_DESCRIPTOR_HEAP_DESC dhd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, n_desc, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(dml.d3d_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)))) return false;

    DmlBuffer pbuf, tbuf;
    if (ep.PersistentResourceSize > 0 && !mk(ep.PersistentResourceSize, pbuf)) return false;
    uint64_t tsz = std::max(ep.TemporaryResourceSize, ip.TemporaryResourceSize);
    if (tsz > 0 && !mk(tsz, tbuf)) return false;

    DML_BINDING_TABLE_DESC btd{ init.Get(), heap->GetCPUDescriptorHandleForHeapStart(), heap->GetGPUDescriptorHandleForHeapStart(), n_desc };
    ComPtr<IDMLBindingTable> bt;
    if (FAILED(dml.dml_device->CreateBindingTable(&btd, IID_PPV_ARGS(&bt)))) return false;
    if (ip.TemporaryResourceSize > 0) {
        DML_BUFFER_BINDING tb{ tbuf.res.Get(), 0, ip.TemporaryResourceSize };
        DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
        bt->BindTemporaryResource(&td);
    }
    if (ep.PersistentResourceSize > 0) {
        DML_BUFFER_BINDING pb{ pbuf.res.Get(), 0, ep.PersistentResourceSize };
        DML_BINDING_DESC pd{ DML_BINDING_TYPE_BUFFER, &pb };
        bt->BindOutputs(1, &pd);
    } else {
        DML_BINDING_DESC nd{ DML_BINDING_TYPE_NONE, nullptr };
        bt->BindOutputs(1, &nd);
    }
    dml.cmd_alloc->Reset();
    dml.cmd_list->Reset(dml.cmd_alloc.Get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = { heap.Get() };
    dml.cmd_list->SetDescriptorHeaps(1, heaps);
    dml.cmd_recorder->RecordDispatch(dml.cmd_list.Get(), init.Get(), bt.Get());
    dml.cmd_list->Close();
    ID3D12CommandList* lists[] = { dml.cmd_list.Get() };
    dml.cmd_queue->ExecuteCommandLists(1, lists);
    dml.wait_gpu();

    btd.Dispatchable = cop.Get();
    bt->Reset(&btd);
    DML_BUFFER_BINDING ib[3] = { { bA.res.Get(), 0, M * K * 4 }, { bB.res.Get(), 0, K * N * 4 }, { bBias.res.Get(), 0, N * 4 } };
    DML_BINDING_DESC id[3] = { { DML_BINDING_TYPE_BUFFER, &ib[0] }, { DML_BINDING_TYPE_BUFFER, &ib[1] }, { DML_BINDING_TYPE_BUFFER, &ib[2] } };
    bt->BindInputs(3, id);
    DML_BUFFER_BINDING ob{ bC.res.Get(), 0, M * N * 4 };
    DML_BINDING_DESC od2{ DML_BINDING_TYPE_BUFFER, &ob };
    bt->BindOutputs(1, &od2);
    if (ep.TemporaryResourceSize > 0) {
        DML_BUFFER_BINDING tb{ tbuf.res.Get(), 0, ep.TemporaryResourceSize };
        DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
        bt->BindTemporaryResource(&td);
    }
    if (ep.PersistentResourceSize > 0) {
        DML_BUFFER_BINDING pb{ pbuf.res.Get(), 0, ep.PersistentResourceSize };
        DML_BINDING_DESC pd{ DML_BINDING_TYPE_BUFFER, &pb };
        bt->BindPersistentResource(&pd);
    }
    dml.cmd_alloc->Reset();
    dml.cmd_list->Reset(dml.cmd_alloc.Get(), nullptr);
    dml.cmd_list->SetDescriptorHeaps(1, heaps);
    dml.cmd_recorder->RecordDispatch(dml.cmd_list.Get(), cop.Get(), bt.Get());
    dml.cmd_list->Close();
    dml.cmd_queue->ExecuteCommandLists(1, lists);
    dml.wait_gpu();
    dml.download_buffer(bC, C_dml.data(), M * N * 4);
    float err = bench_model::max_abs_diff(C_ref.data(), C_dml.data(), M * N);
    std::printf("  DML standalone GEMM (4x64 * 64x128 + bias) max_abs_err = %.3e\n", err);

    auto run_unary = [&](DML_OPERATOR_TYPE op_type, const char* name, auto ref_fn) -> bool {
        const uint32_t U = 1024;
        std::vector<float> in(U), out_r(U), out_d(U);
        fill_deterministic(in, 555, 8.0f);
        for (uint32_t i = 0; i < U; ++i) out_r[i] = ref_fn(in[i]);

        DmlBuffer bi, bo;
        if (!mk(U * 4, bi) || !mk(U * 4, bo)) return false;
        if (!dml.upload_buffer(bi, in.data(), U * 4)) return false;

        UINT s[4] = { 1, 1, 1, U };
        DML_BUFFER_TENSOR_DESC bti{ DML_TENSOR_DATA_TYPE_FLOAT32, DML_TENSOR_FLAG_NONE, 4, s, nullptr, U * 4, 0 };
        DML_BUFFER_TENSOR_DESC bto{ DML_TENSOR_DATA_TYPE_FLOAT32, DML_TENSOR_FLAG_NONE, 4, s, nullptr, U * 4, 0 };
        DML_TENSOR_DESC ti{ DML_TENSOR_TYPE_BUFFER, &bti };
        DML_TENSOR_DESC to{ DML_TENSOR_TYPE_BUFFER, &bto };
        DML_ACTIVATION_SIGMOID_OPERATOR_DESC udesc{ &ti, &to };
        DML_OPERATOR_DESC uod{ op_type, &udesc };

        ComPtr<IDMLOperator> uop;
        ComPtr<IDMLCompiledOperator> ucop;
        if (FAILED(dml.dml_device->CreateOperator(&uod, IID_PPV_ARGS(&uop)))) return false;
        if (FAILED(dml.dml_device->CompileOperator(uop.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&ucop)))) return false;

        IDMLCompiledOperator* ucops[] = { ucop.Get() };
        ComPtr<IDMLOperatorInitializer> uinit;
        if (FAILED(dml.dml_device->CreateOperatorInitializer(1, ucops, IID_PPV_ARGS(&uinit)))) return false;
        DML_BINDING_PROPERTIES uep = ucop->GetBindingProperties();
        DML_BINDING_PROPERTIES uip = uinit->GetBindingProperties();
        UINT und = std::max<UINT>({ uep.RequiredDescriptorCount, uip.RequiredDescriptorCount, 1 });
        D3D12_DESCRIPTOR_HEAP_DESC udhd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, und, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        ComPtr<ID3D12DescriptorHeap> uheap;
        if (FAILED(dml.d3d_device->CreateDescriptorHeap(&udhd, IID_PPV_ARGS(&uheap)))) return false;
        DmlBuffer upb, utb;
        if (uep.PersistentResourceSize > 0 && !mk(uep.PersistentResourceSize, upb)) return false;
        uint64_t utsz = std::max(uep.TemporaryResourceSize, uip.TemporaryResourceSize);
        if (utsz > 0 && !mk(utsz, utb)) return false;

        DML_BINDING_TABLE_DESC ubtd{ uinit.Get(), uheap->GetCPUDescriptorHandleForHeapStart(), uheap->GetGPUDescriptorHandleForHeapStart(), und };
        ComPtr<IDMLBindingTable> ubt;
        if (FAILED(dml.dml_device->CreateBindingTable(&ubtd, IID_PPV_ARGS(&ubt)))) return false;
        if (uip.TemporaryResourceSize > 0) {
            DML_BUFFER_BINDING tb{ utb.res.Get(), 0, uip.TemporaryResourceSize };
            DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
            ubt->BindTemporaryResource(&td);
        }
        if (uep.PersistentResourceSize > 0) {
            DML_BUFFER_BINDING pb{ upb.res.Get(), 0, uep.PersistentResourceSize };
            DML_BINDING_DESC pd{ DML_BINDING_TYPE_BUFFER, &pb };
            ubt->BindOutputs(1, &pd);
        } else {
            DML_BINDING_DESC nd{ DML_BINDING_TYPE_NONE, nullptr };
            ubt->BindOutputs(1, &nd);
        }
        dml.cmd_alloc->Reset();
        dml.cmd_list->Reset(dml.cmd_alloc.Get(), nullptr);
        ID3D12DescriptorHeap* uheaps[] = { uheap.Get() };
        dml.cmd_list->SetDescriptorHeaps(1, uheaps);
        dml.cmd_recorder->RecordDispatch(dml.cmd_list.Get(), uinit.Get(), ubt.Get());
        dml.cmd_list->Close();
        ID3D12CommandList* ulists[] = { dml.cmd_list.Get() };
        dml.cmd_queue->ExecuteCommandLists(1, ulists);
        dml.wait_gpu();

        ubtd.Dispatchable = ucop.Get();
        ubt->Reset(&ubtd);
        DML_BUFFER_BINDING uib{ bi.res.Get(), 0, U * 4 };
        DML_BINDING_DESC uid{ DML_BINDING_TYPE_BUFFER, &uib };
        ubt->BindInputs(1, &uid);
        DML_BUFFER_BINDING uob{ bo.res.Get(), 0, U * 4 };
        DML_BINDING_DESC uod2{ DML_BINDING_TYPE_BUFFER, &uob };
        ubt->BindOutputs(1, &uod2);
        if (uep.TemporaryResourceSize > 0) {
            DML_BUFFER_BINDING tb{ utb.res.Get(), 0, uep.TemporaryResourceSize };
            DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
            ubt->BindTemporaryResource(&td);
        }
        if (uep.PersistentResourceSize > 0) {
            DML_BUFFER_BINDING pb{ upb.res.Get(), 0, uep.PersistentResourceSize };
            DML_BINDING_DESC pd{ DML_BINDING_TYPE_BUFFER, &pb };
            ubt->BindPersistentResource(&pd);
        }
        dml.cmd_alloc->Reset();
        dml.cmd_list->Reset(dml.cmd_alloc.Get(), nullptr);
        dml.cmd_list->SetDescriptorHeaps(1, uheaps);
        dml.cmd_recorder->RecordDispatch(dml.cmd_list.Get(), ucop.Get(), ubt.Get());
        dml.cmd_list->Close();
        dml.cmd_queue->ExecuteCommandLists(1, ulists);
        dml.wait_gpu();
        dml.download_buffer(bo, out_d.data(), U * 4);
        float uerr = bench_model::max_abs_diff(out_r.data(), out_d.data(), U);
        std::printf("  DML standalone %s max_abs_err = %.3e\n", name, uerr);
        return uerr < 1e-4f;
    };

    bool ok_sig = run_unary(DML_OPERATOR_ACTIVATION_SIGMOID, "Sigmoid", bench_model::ref_sigmoid);
    bool ok_tanh = run_unary(DML_OPERATOR_ACTIVATION_TANH, "Tanh   ", bench_model::ref_tanh);
    return err < 1e-4f && ok_sig && ok_tanh;
}

}

int main() {
    using namespace bench_model;

    std::printf("=== Unibox 3-Tier Compute Runtime Probe & Benchmark ===\n");
    std::printf("Model: IN=%u -> %u (SiLU) -> %u (SiLU) -> %u (SiLU) -> GRU(%u) -> OUT=%u | Total Params: %u (%.2f KB fp32)\n\n",
        ModelConfig::IN_DIM, ModelConfig::H1, ModelConfig::H2, ModelConfig::H3,
        ModelConfig::GRU_H, ModelConfig::OUT_DIM,
        ModelConfig::TOTAL_PARAMS, (ModelConfig::TOTAL_PARAMS * 4.0) / 1024.0);

    auto cpu_cap = bench_cpu::detect_cpu();
    std::printf("[CPU] Vendor: %s | Brand: %s\n", cpu_cap.vendor, cpu_cap.brand);
    std::printf("[CPU] SSE2=%d AVX=%d OSXSAVE=%d FMA=%d AVX2=%d | AVX2+FMA Active=%d\n\n",
        cpu_cap.sse2, cpu_cap.avx, cpu_cap.osxsave, cpu_cap.fma, cpu_cap.avx2,
        bench_cpu::is_avx2_fma_supported() ? 1 : 0);

    ModelWeights weights;
    weights.init(42);

    std::printf("--- Tier 3: CPU AVX2+FMA Accuracy & Benchmarks ---\n");
    {
        std::vector<float> test_in(1024), ref_s(1024), avx_s(1024);
        fill_deterministic(test_in, 777, 10.0f);
        for (size_t i = 0; i < 1024; ++i) ref_s[i] = ref_silu(test_in[i]);
        bench_cpu::avx2_silu(test_in.data(), avx_s.data(), 1024);
        std::printf("  AVX2 SiLU max_abs_err    = %.3e\n", max_abs_diff(ref_s.data(), avx_s.data(), 1024));
        for (size_t i = 0; i < 1024; ++i) ref_s[i] = ref_sigmoid(test_in[i]);
        bench_cpu::avx2_sigmoid(test_in.data(), avx_s.data(), 1024);
        std::printf("  AVX2 Sigmoid max_abs_err = %.3e\n", max_abs_diff(ref_s.data(), avx_s.data(), 1024));
        for (size_t i = 0; i < 1024; ++i) ref_s[i] = ref_tanh(test_in[i]);
        bench_cpu::avx2_tanh(test_in.data(), avx_s.data(), 1024);
        std::printf("  AVX2 Tanh max_abs_err    = %.3e\n", max_abs_diff(ref_s.data(), avx_s.data(), 1024));

        bench_cpu::CpuWorkspace ws;
        ws.ensure(64);
        for (uint32_t b : { 1u, 4u, 16u, 32u }) {
            std::vector<float> x(b * ModelConfig::IN_DIM), h0(b * ModelConfig::GRU_H);
            fill_deterministic(x, 100 + b, 1.0f);
            fill_deterministic(h0, 200 + b, 0.5f);
            std::vector<float> out_ref(b * ModelConfig::OUT_DIM), h1_ref(b * ModelConfig::GRU_H);
            std::vector<float> out_avx(b * ModelConfig::OUT_DIM), h1_avx(b * ModelConfig::GRU_H);
            ref_forward(weights, x.data(), h0.data(), out_ref.data(), h1_ref.data(), b);
            bench_cpu::avx2_forward(weights, x.data(), h0.data(), out_avx.data(), h1_avx.data(), ws, b);
            float e_out = max_abs_diff(out_ref.data(), out_avx.data(), out_ref.size());
            float e_h = max_abs_diff(h1_ref.data(), h1_avx.data(), h1_ref.size());
            std::printf("  AVX2 Forward batch=%2u: max_abs_err(out)=%.3e, max_abs_err(h)=%.3e\n", b, e_out, e_h);
        }

        std::vector<float> p_ref = weights.params, p_avx = weights.params;
        std::vector<float> grads(ModelConfig::TOTAL_PARAMS);
        fill_deterministic(grads, 999, 0.05f);
        std::vector<float> m_ref(ModelConfig::TOTAL_PARAMS, 0.0f), v_ref(ModelConfig::TOTAL_PARAMS, 0.0f);
        std::vector<float> m_avx(ModelConfig::TOTAL_PARAMS, 0.0f), v_avx(ModelConfig::TOTAL_PARAMS, 0.0f);
        float inv_bc1 = 1.0f / (1.0f - 0.9f);
        float inv_bc2 = 1.0f / (1.0f - 0.999f);
        ref_adamw_step(p_ref.data(), grads.data(), m_ref.data(), v_ref.data(), ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
        bench_cpu::avx2_adamw_step(p_avx.data(), grads.data(), m_avx.data(), v_avx.data(), ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
        std::printf("  AVX2 AdamW (181K params) max_abs_err = %.3e\n", max_abs_diff(p_ref.data(), p_avx.data(), p_ref.size()));

        std::vector<float> x1(ModelConfig::IN_DIM), h0_1(ModelConfig::GRU_H), out1(ModelConfig::OUT_DIM), h1_1(ModelConfig::GRU_H);
        fill_deterministic(x1, 101, 1.0f); fill_deterministic(h0_1, 201, 0.5f);
        std::vector<float> out1_sse(ModelConfig::OUT_DIM), h1_1_sse(ModelConfig::GRU_H), out1_ref(ModelConfig::OUT_DIM), h1_1_ref(ModelConfig::GRU_H);
        ref_forward(weights, x1.data(), h0_1.data(), out1_ref.data(), h1_1_ref.data(), 1);
        bench_cpu::sse2_forward(weights, x1.data(), h0_1.data(), out1_sse.data(), h1_1_sse.data(), ws, 1);
        std::printf("  SSE2 Forward batch= 1: max_abs_err(out)=%.3e, max_abs_err(h)=%.3e\n",
            max_abs_diff(out1_ref.data(), out1_sse.data(), out1_ref.size()),
            max_abs_diff(h1_1_ref.data(), h1_1_sse.data(), h1_1_ref.size()));

        print_stats("CPU Scalar Forward (batch=1)", bench_fn(50, 500, [&]() {
            ref_forward(weights, x1.data(), h0_1.data(), out1.data(), h1_1.data(), 1);
        }));
        print_stats("CPU SSE2 Fallback Forward (batch=1)", bench_fn(100, 1000, [&]() {
            bench_cpu::sse2_forward(weights, x1.data(), h0_1.data(), out1.data(), h1_1.data(), ws, 1);
        }));
        print_stats("CPU AVX2+FMA Forward (batch=1)", bench_fn(500, 5000, [&]() {
            bench_cpu::avx2_forward(weights, x1.data(), h0_1.data(), out1.data(), h1_1.data(), ws, 1);
        }));

        std::vector<float> x16(16 * ModelConfig::IN_DIM), h0_16(16 * ModelConfig::GRU_H), out16(16 * ModelConfig::OUT_DIM), h1_16(16 * ModelConfig::GRU_H);
        fill_deterministic(x16, 116, 1.0f); fill_deterministic(h0_16, 216, 0.5f);
        print_stats("CPU AVX2+FMA Forward (batch=16)", bench_fn(200, 2000, [&]() {
            bench_cpu::avx2_forward(weights, x16.data(), h0_16.data(), out16.data(), h1_16.data(), ws, 16);
        }));

        std::vector<float> x32(32 * ModelConfig::IN_DIM), h0_32(32 * ModelConfig::GRU_H), out32(32 * ModelConfig::OUT_DIM), h1_32(32 * ModelConfig::GRU_H);
        fill_deterministic(x32, 132, 1.0f); fill_deterministic(h0_32, 232, 0.5f);
        print_stats("CPU AVX2+FMA Forward (batch=32)", bench_fn(100, 1000, [&]() {
            bench_cpu::avx2_forward(weights, x32.data(), h0_32.data(), out32.data(), h1_32.data(), ws, 32);
        }));
        print_stats("CPU AVX2+FMA AdamW Step (181K params)", bench_fn(100, 1000, [&]() {
            bench_cpu::avx2_adamw_step(p_avx.data(), grads.data(), m_avx.data(), v_avx.data(), ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
        }));
        print_stats("CPU AVX2+FMA Train Step (b=32 fwd+AdamW)", bench_fn(100, 1000, [&]() {
            bench_cpu::avx2_forward(weights, x32.data(), h0_32.data(), out32.data(), h1_32.data(), ws, 32);
            bench_cpu::avx2_adamw_step(p_avx.data(), grads.data(), m_avx.data(), v_avx.data(), ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
        }));
    }

    std::printf("\n--- Tier 1: NVIDIA CUDA Driver API (nvcuda.dll + PTX JIT) ---\n");
    bench_cuda::CudaBackend cuda;
    if (cuda.init(0)) {
        size_t free_before = 0, total_mem = 0;
        cuda.api.cuMemGetInfo_v2(&free_before, &total_mem);
        std::printf("[CUDA] Device %d: %s | CC %d.%d | SMs: %d | VRAM Total: %.1f MiB | Free: %.1f MiB\n",
            cuda.info.ordinal, cuda.info.name.c_str(),
            cuda.info.cc_major, cuda.info.cc_minor, cuda.info.sm_count,
            cuda.info.total_vram_bytes / (1024.0 * 1024.0),
            free_before / (1024.0 * 1024.0));

        if (cuda.allocate_model(weights, 64)) {
            size_t free_after = 0;
            cuda.api.cuMemGetInfo_v2(&free_after, &total_mem);
            std::printf("[CUDA] Model buffers allocated: %.2f MiB (driver free delta: %.2f MiB)\n",
                cuda.allocated_bytes / (1024.0 * 1024.0),
                (free_before - free_after) / (1024.0 * 1024.0));

            std::vector<float> test_in(1024), ref_s(1024), gpu_s(1024);
            fill_deterministic(test_in, 777, 10.0f);
            bench_cuda::CUdeviceptr d_in = cuda.alloc_bytes(1024 * 4);
            bench_cuda::CUdeviceptr d_out_t = cuda.alloc_bytes(1024 * 4);
            cuda.api.cuMemcpyHtoD_v2(d_in, test_in.data(), 1024 * 4);

            for (size_t i = 0; i < 1024; ++i) ref_s[i] = ref_silu(test_in[i]);
            cuda.launch_elem(cuda.fn_silu, d_in, d_out_t, 1024);
            cuda.api.cuStreamSynchronize(cuda.stream);
            cuda.api.cuMemcpyDtoH_v2(gpu_s.data(), d_out_t, 1024 * 4);
            std::printf("  CUDA PTX SiLU max_abs_err    = %.3e\n", max_abs_diff(ref_s.data(), gpu_s.data(), 1024));

            for (size_t i = 0; i < 1024; ++i) ref_s[i] = ref_sigmoid(test_in[i]);
            cuda.launch_elem(cuda.fn_sigmoid, d_in, d_out_t, 1024);
            cuda.api.cuStreamSynchronize(cuda.stream);
            cuda.api.cuMemcpyDtoH_v2(gpu_s.data(), d_out_t, 1024 * 4);
            std::printf("  CUDA PTX Sigmoid max_abs_err = %.3e\n", max_abs_diff(ref_s.data(), gpu_s.data(), 1024));

            for (size_t i = 0; i < 1024; ++i) ref_s[i] = ref_tanh(test_in[i]);
            cuda.launch_elem(cuda.fn_tanh, d_in, d_out_t, 1024);
            cuda.api.cuStreamSynchronize(cuda.stream);
            cuda.api.cuMemcpyDtoH_v2(gpu_s.data(), d_out_t, 1024 * 4);
            std::printf("  CUDA PTX Tanh max_abs_err    = %.3e\n", max_abs_diff(ref_s.data(), gpu_s.data(), 1024));

            cuda.api.cuMemFree_v2(d_in);
            cuda.api.cuMemFree_v2(d_out_t);

            for (uint32_t b : { 1u, 4u, 16u, 64u }) {
                std::vector<float> x(b * ModelConfig::IN_DIM), h0(b * ModelConfig::GRU_H);
                fill_deterministic(x, 100 + b, 1.0f);
                fill_deterministic(h0, 200 + b, 0.5f);
                std::vector<float> out_ref(b * ModelConfig::OUT_DIM), h1_ref(b * ModelConfig::GRU_H);
                std::vector<float> out_gpu(b * ModelConfig::OUT_DIM), h1_gpu(b * ModelConfig::GRU_H);
                ref_forward(weights, x.data(), h0.data(), out_ref.data(), h1_ref.data(), b);

                cuda.api.cuMemcpyHtoD_v2(cuda.d_x, x.data(), x.size() * 4);
                cuda.api.cuMemcpyHtoD_v2(cuda.d_h0, h0.data(), h0.size() * 4);
                cuda.forward_async(weights, b);
                cuda.api.cuStreamSynchronize(cuda.stream);
                cuda.api.cuMemcpyDtoH_v2(out_gpu.data(), cuda.d_out, out_gpu.size() * 4);
                cuda.api.cuMemcpyDtoH_v2(h1_gpu.data(), cuda.d_h1, h1_gpu.size() * 4);
                std::printf("  CUDA Forward batch=%2u: max_abs_err(out)=%.3e, max_abs_err(h)=%.3e\n",
                    b, max_abs_diff(out_ref.data(), out_gpu.data(), out_ref.size()),
                    max_abs_diff(h1_ref.data(), h1_gpu.data(), h1_ref.size()));
            }

            std::vector<float> p_ref = weights.params, p_gpu(ModelConfig::TOTAL_PARAMS);
            std::vector<float> grads(ModelConfig::TOTAL_PARAMS);
            fill_deterministic(grads, 999, 0.05f);
            std::vector<float> m_ref(ModelConfig::TOTAL_PARAMS, 0.0f), v_ref(ModelConfig::TOTAL_PARAMS, 0.0f);
            float inv_bc1 = 1.0f / (1.0f - 0.9f);
            float inv_bc2 = 1.0f / (1.0f - 0.999f);
            ref_adamw_step(p_ref.data(), grads.data(), m_ref.data(), v_ref.data(), ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);

            std::vector<float> zeros(ModelConfig::TOTAL_PARAMS, 0.0f);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_params, weights.params.data(), ModelConfig::TOTAL_PARAMS * 4);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_grads, grads.data(), ModelConfig::TOTAL_PARAMS * 4);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_m, zeros.data(), ModelConfig::TOTAL_PARAMS * 4);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_v, zeros.data(), ModelConfig::TOTAL_PARAMS * 4);
            cuda.adamw_async(ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
            cuda.api.cuStreamSynchronize(cuda.stream);
            cuda.api.cuMemcpyDtoH_v2(p_gpu.data(), cuda.d_params, ModelConfig::TOTAL_PARAMS * 4);
            std::printf("  CUDA AdamW (181K params) max_abs_err = %.3e\n", max_abs_diff(p_ref.data(), p_gpu.data(), p_ref.size()));

            bench_cuda::CUevent ev_start = nullptr, ev_end = nullptr;
            cuda.api.cuEventCreate(&ev_start, 0);
            cuda.api.cuEventCreate(&ev_end, 0);

            for (uint32_t b : { 1u, 16u, 64u }) {
                std::vector<float> x(b * ModelConfig::IN_DIM), h0(b * ModelConfig::GRU_H), out(b * ModelConfig::OUT_DIM);
                fill_deterministic(x, 300 + b, 1.0f);
                fill_deterministic(h0, 400 + b, 0.5f);
                cuda.api.cuMemcpyHtoD_v2(cuda.d_x, x.data(), x.size() * 4);
                cuda.api.cuMemcpyHtoD_v2(cuda.d_h0, h0.data(), h0.size() * 4);

                for (int i = 0; i < 100; ++i) {
                    cuda.forward_async(weights, b);
                }
                cuda.api.cuStreamSynchronize(cuda.stream);

                std::vector<double> gpu_only_us(500);
                for (int i = 0; i < 500; ++i) {
                    cuda.api.cuEventRecord(ev_start, cuda.stream);
                    cuda.forward_async(weights, b);
                    cuda.api.cuEventRecord(ev_end, cuda.stream);
                    cuda.api.cuEventSynchronize(ev_end);
                    float ms = 0.0f;
                    cuda.api.cuEventElapsedTime(&ms, ev_start, ev_end);
                    gpu_only_us[i] = static_cast<double>(ms) * 1000.0;
                }
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "CUDA GPU-only Event Forward (b=%u)", b);
                print_stats(lbl, compute_stats(gpu_only_us));

                std::snprintf(lbl, sizeof(lbl), "CUDA Host Sync Forward (b=%u)", b);
                print_stats(lbl, bench_fn(100, 500, [&]() {
                    cuda.forward_async(weights, b);
                    cuda.api.cuStreamSynchronize(cuda.stream);
                }));

                std::snprintf(lbl, sizeof(lbl), "CUDA HtoD+Fwd+DtoH Sync (b=%u)", b);
                print_stats(lbl, bench_fn(100, 500, [&]() {
                    cuda.api.cuMemcpyHtoDAsync_v2(cuda.d_x, x.data(), x.size() * 4, cuda.stream);
                    cuda.forward_async(weights, b);
                    cuda.api.cuMemcpyDtoHAsync_v2(out.data(), cuda.d_out, out.size() * 4, cuda.stream);
                    cuda.api.cuStreamSynchronize(cuda.stream);
                }));
            }

            print_stats("CUDA AdamW Kernel Sync (181K params)", bench_fn(100, 500, [&]() {
                cuda.adamw_async(ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
                cuda.api.cuStreamSynchronize(cuda.stream);
            }));

            print_stats("CUDA Online Update (b=64 fwd+AdamW)", bench_fn(100, 500, [&]() {
                cuda.forward_async(weights, 64);
                cuda.adamw_async(ModelConfig::TOTAL_PARAMS, 1e-3f, 0.9f, 0.999f, 1e-8f, 1e-4f, inv_bc1, inv_bc2);
                cuda.api.cuStreamSynchronize(cuda.stream);
            }));

            cuda.api.cuEventDestroy_v2(ev_start);
            cuda.api.cuEventDestroy_v2(ev_end);
        }
        cuda.destroy();
    } else {
        std::printf("[CUDA] Initialization failed or no CUDA device found.\n");
    }

    std::printf("\n--- Tier 2: DirectML + D3D12 (dxgi.dll + d3d12.dll + directml.dll) ---\n");
    bench_dml::DmlBackend dml;
    if (dml.api.load()) {
        auto adapters = bench_dml::enumerate_dxgi_adapters(dml.api);
        std::printf("[DXGI] Found %zu adapters:\n", adapters.size());
        for (const auto& a : adapters) {
            std::printf("  [%u] %ls | Vendor=0x%04X Device=0x%04X | VRAM=%.1f MiB | SW=%d\n",
                a.index, a.description.c_str(), a.vendor_id, a.device_id,
                a.dedicated_vram / (1024.0 * 1024.0), a.is_software ? 1 : 0);
        }
        dml.api.unload();
    }

    if (dml.init(-1)) {
        std::printf("[DirectML] Selected Adapter [%u]: %ls | Max Feature Level: 0x%04X\n",
            dml.selected_adapter.index, dml.selected_adapter.description.c_str(),
            static_cast<unsigned>(dml.max_feature_level));

        test_dml_standalone_ops(dml);

        for (uint32_t b : { 1u, 16u }) {
            if (!dml.build_model_graph(weights, b)) {
                std::printf("  DML build_model_graph(b=%u) failed!\n", b);
                continue;
            }
            std::vector<float> x(b * ModelConfig::IN_DIM), h0(b * ModelConfig::GRU_H);
            fill_deterministic(x, 100 + b, 1.0f);
            fill_deterministic(h0, 200 + b, 0.5f);
            std::vector<float> out_ref(b * ModelConfig::OUT_DIM), h1_ref(b * ModelConfig::GRU_H);
            std::vector<float> out_dml(b * ModelConfig::OUT_DIM), h1_dml(b * ModelConfig::GRU_H);
            ref_forward(weights, x.data(), h0.data(), out_ref.data(), h1_ref.data(), b);

            dml.upload_buffer(dml.buf_x, x.data(), x.size() * 4);
            dml.upload_buffer(dml.buf_h0, h0.data(), h0.size() * 4);
            dml.execute_recorded_fresh();
            dml.download_buffer(dml.buf_out, out_dml.data(), out_dml.size() * 4);
            dml.download_buffer(dml.buf_h1, h1_dml.data(), h1_dml.size() * 4);
            std::printf("  DML Pipeline Forward b=%2u: max_abs_err(out)=%.3e, max_abs_err(h)=%.3e | VRAM alloc=%.2f MiB\n",
                b, max_abs_diff(out_ref.data(), out_dml.data(), out_ref.size()),
                max_abs_diff(h1_ref.data(), h1_dml.data(), h1_ref.size()),
                dml.gpu_bytes_allocated / (1024.0 * 1024.0));

            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "DML Record+Exec+Wait Forward (b=%u)", b);
            print_stats(lbl, bench_fn(50, 300, [&]() {
                dml.execute_recorded_fresh();
            }));

            std::snprintf(lbl, sizeof(lbl), "DML PreRecorded Exec+Wait (b=%u)", b);
            print_stats(lbl, bench_fn(50, 300, [&]() {
                dml.execute_prerecorded();
            }));

            if (dml.ts_freq > 0) {
                for (int i = 0; i < 20; ++i) dml.execute_gpu_timed();
                std::vector<double> ts_samples(200);
                for (int i = 0; i < 200; ++i) ts_samples[i] = dml.execute_gpu_timed();
                std::snprintf(lbl, sizeof(lbl), "DML GPU-only Timestamp (b=%u)", b);
                print_stats(lbl, compute_stats(ts_samples));
            }
        }
        dml.destroy();
    } else {
        std::printf("[DirectML] Initialization failed.\n");
    }

    std::printf("\n--- Auto-Selection Probe (CUDA -> DirectML -> CPU AVX2 -> CPU SSE2) ---\n");
    {
        bench_cuda::CudaBackend probe_cuda;
        if (probe_cuda.init(0)) {
            std::printf("  Selected Tier 1: NVIDIA CUDA Driver API (%s, CC %d.%d, %d SMs)\n",
                probe_cuda.info.name.c_str(), probe_cuda.info.cc_major, probe_cuda.info.cc_minor, probe_cuda.info.sm_count);
            probe_cuda.destroy();
        } else {
            bench_dml::DmlBackend probe_dml;
            if (probe_dml.init(-1)) {
                std::printf("  Selected Tier 2: DirectML (%ls)\n", probe_dml.selected_adapter.description.c_str());
                probe_dml.destroy();
            } else if (bench_cpu::is_avx2_fma_supported()) {
                std::printf("  Selected Tier 3a: CPU AVX2+FMA\n");
            } else {
                std::printf("  Selected Tier 3b: CPU SSE2\n");
            }
        }
    }

    return 0;
}
