#pragma once

#include "common_model.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <DirectML.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bench_dml {

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

struct DmlBackend {
    DmlApi api;
    ComPtr<ID3D12Device> d3d_device;
    ComPtr<ID3D12CommandQueue> cmd_queue;
    ComPtr<ID3D12CommandAllocator> cmd_alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd_list;
    ComPtr<ID3D12CommandAllocator> prerecord_alloc;
    ComPtr<ID3D12GraphicsCommandList> prerecord_list;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    uint64_t fence_val = 0;

    ComPtr<ID3D12QueryHeap> ts_heap;
    DmlBuffer ts_readback;
    uint64_t ts_freq = 0;

    ComPtr<IDMLDevice> dml_device;
    ComPtr<IDMLCommandRecorder> cmd_recorder;

    DxgiAdapterInfo selected_adapter{};
    DML_FEATURE_LEVEL max_feature_level = DML_FEATURE_LEVEL_1_0;
    size_t gpu_bytes_allocated = 0;

    ComPtr<ID3D12DescriptorHeap> desc_heap;
    UINT desc_inc_size = 0;

    static constexpr int NUM_OPS = 11;
    ComPtr<IDMLCompiledOperator> ops[NUM_OPS];
    ComPtr<IDMLBindingTable> op_bindings[NUM_OPS];
    DmlBuffer op_persistent[NUM_OPS];
    DmlBuffer shared_temp;

    DmlBuffer buf_x, buf_h0;
    DmlBuffer buf_w1, buf_b1, buf_a1p, buf_s1, buf_a1;
    DmlBuffer buf_w2, buf_b2, buf_a2p, buf_s2, buf_a2;
    DmlBuffer buf_w3, buf_b3, buf_a3p, buf_s3, buf_a3;
    DmlBuffer buf_gru_w, buf_gru_r, buf_gru_b;
    DmlBuffer buf_wout, buf_bout;
    DmlBuffer buf_out, buf_h1;

    DmlBuffer upload_buf;
    DmlBuffer readback_buf;
    uint32_t current_batch = 0;

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

        HRESULT hr = d3d_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, init_state, nullptr, IID_PPV_ARGS(&out.res));
        if (FAILED(hr)) return false;
        out.size = alloc_size;
        if (heap_type == D3D12_HEAP_TYPE_DEFAULT) {
            gpu_bytes_allocated += alloc_size;
        }
        return true;
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
        HRESULT hr = api.fn_CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return false;
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(static_cast<UINT>(chosen), &adapter);
        if (FAILED(hr)) return false;

        hr = api.fn_D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d_device));
        if (FAILED(hr)) return false;

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(d3d_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&cmd_queue)))) return false;
        if (FAILED(d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc)))) return false;
        if (FAILED(d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), nullptr, IID_PPV_ARGS(&cmd_list)))) return false;
        cmd_list->Close();
        if (FAILED(d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&prerecord_alloc)))) return false;
        if (FAILED(d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, prerecord_alloc.Get(), nullptr, IID_PPV_ARGS(&prerecord_list)))) return false;
        prerecord_list->Close();

        if (FAILED(d3d_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event) return false;

        hr = api.fn_DMLCreateDevice(d3d_device.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml_device));
        if (FAILED(hr)) return false;

        const DML_FEATURE_LEVEL levels[] = {
            DML_FEATURE_LEVEL_1_0, DML_FEATURE_LEVEL_2_0, DML_FEATURE_LEVEL_2_1,
            DML_FEATURE_LEVEL_3_0, DML_FEATURE_LEVEL_3_1, DML_FEATURE_LEVEL_4_0,
            DML_FEATURE_LEVEL_4_1, DML_FEATURE_LEVEL_5_0, DML_FEATURE_LEVEL_5_1,
            DML_FEATURE_LEVEL_5_2, DML_FEATURE_LEVEL_6_0, DML_FEATURE_LEVEL_6_1,
            DML_FEATURE_LEVEL_6_2, DML_FEATURE_LEVEL_6_3, DML_FEATURE_LEVEL_6_4
        };
        DML_FEATURE_QUERY_FEATURE_LEVELS fq{ _countof(levels), levels };
        DML_FEATURE_DATA_FEATURE_LEVELS fd{};
        if (SUCCEEDED(dml_device->CheckFeatureSupport(DML_FEATURE_FEATURE_LEVELS, sizeof(fq), &fq, sizeof(fd), &fd))) {
            max_feature_level = fd.MaxSupportedFeatureLevel;
        }

        if (FAILED(dml_device->CreateCommandRecorder(IID_PPV_ARGS(&cmd_recorder)))) return false;
        desc_inc_size = d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_QUERY_HEAP_DESC qhd{};
        qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhd.Count = 2;
        if (SUCCEEDED(d3d_device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&ts_heap)))) {
            create_buffer(256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, ts_readback);
            cmd_queue->GetTimestampFrequency(&ts_freq);
        }
        return true;
    }

    bool upload_buffer(DmlBuffer& dst, const void* src, uint64_t bytes) {
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
        cmd_list->CopyBufferRegion(dst.res.Get(), 0, upload_buf.res.Get(), 0, bytes);
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

    bool download_buffer(const DmlBuffer& src, void* dst, uint64_t bytes) {
        if (readback_buf.size < bytes) {
            readback_buf.res.Reset();
            if (!create_buffer(std::max<uint64_t>(bytes, 256 * 1024), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, readback_buf)) {
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
        cmd_list->CopyBufferRegion(readback_buf.res.Get(), 0, src.res.Get(), 0, bytes);
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

    bool build_model_graph(const bench_model::ModelWeights& w, uint32_t batch) {
        using C = bench_model::ModelConfig;
        current_batch = batch;
        for (int i = 0; i < NUM_OPS; ++i) {
            ops[i].Reset();
            op_bindings[i].Reset();
            op_persistent[i].res.Reset();
        }
        desc_heap.Reset();
        shared_temp.res.Reset();
        buf_x.res.Reset(); buf_h0.res.Reset();
        buf_w1.res.Reset(); buf_b1.res.Reset(); buf_a1p.res.Reset(); buf_s1.res.Reset(); buf_a1.res.Reset();
        buf_w2.res.Reset(); buf_b2.res.Reset(); buf_a2p.res.Reset(); buf_s2.res.Reset(); buf_a2.res.Reset();
        buf_w3.res.Reset(); buf_b3.res.Reset(); buf_a3p.res.Reset(); buf_s3.res.Reset(); buf_a3.res.Reset();
        buf_gru_w.res.Reset(); buf_gru_r.res.Reset(); buf_gru_b.res.Reset();
        buf_wout.res.Reset(); buf_bout.res.Reset();
        buf_out.res.Reset(); buf_h1.res.Reset();
        gpu_bytes_allocated = 0;

        auto make_uav = [&](uint64_t bytes, DmlBuffer& b) -> bool {
            return create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, b);
        };
        if (!make_uav(batch * C::IN_DIM * 4, buf_x) ||
            !make_uav(batch * C::GRU_H * 4, buf_h0) ||
            !make_uav(C::W1_SIZE * 4, buf_w1) || !make_uav(C::B1_SIZE * 4, buf_b1) ||
            !make_uav(batch * C::H1 * 4, buf_a1p) || !make_uav(batch * C::H1 * 4, buf_s1) || !make_uav(batch * C::H1 * 4, buf_a1) ||
            !make_uav(C::W2_SIZE * 4, buf_w2) || !make_uav(C::B2_SIZE * 4, buf_b2) ||
            !make_uav(batch * C::H2 * 4, buf_a2p) || !make_uav(batch * C::H2 * 4, buf_s2) || !make_uav(batch * C::H2 * 4, buf_a2) ||
            !make_uav(C::W3_SIZE * 4, buf_w3) || !make_uav(C::B3_SIZE * 4, buf_b3) ||
            !make_uav(batch * C::H3 * 4, buf_a3p) || !make_uav(batch * C::H3 * 4, buf_s3) || !make_uav(batch * C::H3 * 4, buf_a3) ||
            !make_uav(C::W_IH_SIZE * 4, buf_gru_w) || !make_uav(C::W_HH_SIZE * 4, buf_gru_r) ||
            !make_uav((C::B_IH_SIZE + C::B_HH_SIZE) * 4, buf_gru_b) ||
            !make_uav(C::W_OUT_SIZE * 4, buf_wout) || !make_uav(C::B_OUT_SIZE * 4, buf_bout) ||
            !make_uav(batch * C::OUT_DIM * 4, buf_out) || !make_uav(batch * C::GRU_H * 4, buf_h1)) {
            return false;
        }

        const float* p = w.params.data();
        if (!upload_buffer(buf_w1, p + w.off_w1, C::W1_SIZE * 4) ||
            !upload_buffer(buf_b1, p + w.off_b1, C::B1_SIZE * 4) ||
            !upload_buffer(buf_w2, p + w.off_w2, C::W2_SIZE * 4) ||
            !upload_buffer(buf_b2, p + w.off_b2, C::B2_SIZE * 4) ||
            !upload_buffer(buf_w3, p + w.off_w3, C::W3_SIZE * 4) ||
            !upload_buffer(buf_b3, p + w.off_b3, C::B3_SIZE * 4) ||
            !upload_buffer(buf_wout, p + w.off_w_out, C::W_OUT_SIZE * 4) ||
            !upload_buffer(buf_bout, p + w.off_b_out, C::B_OUT_SIZE * 4)) {
            return false;
        }

        std::vector<float> gru_w_t(C::W_IH_SIZE);
        for (uint32_t k = 0; k < C::H3; ++k) {
            for (uint32_t j = 0; j < 3 * C::GRU_H; ++j) {
                gru_w_t[j * C::H3 + k] = p[w.off_w_ih + k * (3 * C::GRU_H) + j];
            }
        }
        std::vector<float> gru_r_t(C::W_HH_SIZE);
        for (uint32_t k = 0; k < C::GRU_H; ++k) {
            for (uint32_t j = 0; j < 3 * C::GRU_H; ++j) {
                gru_r_t[j * C::GRU_H + k] = p[w.off_w_hh + k * (3 * C::GRU_H) + j];
            }
        }
        std::vector<float> gru_b(C::B_IH_SIZE + C::B_HH_SIZE);
        std::memcpy(gru_b.data(), p + w.off_b_ih, C::B_IH_SIZE * 4);
        std::memcpy(gru_b.data() + C::B_IH_SIZE, p + w.off_b_hh, C::B_HH_SIZE * 4);
        if (!upload_buffer(buf_gru_w, gru_w_t.data(), C::W_IH_SIZE * 4) ||
            !upload_buffer(buf_gru_r, gru_r_t.data(), C::W_HH_SIZE * 4) ||
            !upload_buffer(buf_gru_b, gru_b.data(), gru_b.size() * 4)) {
            return false;
        }

        struct TensorSpec {
            UINT sizes[4];
            UINT strides[4];
            DML_BUFFER_TENSOR_DESC bdesc{};
            DML_TENSOR_DESC desc{};
        };
        auto make_tensor = [](TensorSpec& t, UINT d0, UINT d1, UINT d2, UINT d3, bool broadcast_w = false) {
            t.sizes[0] = d0; t.sizes[1] = d1; t.sizes[2] = d2; t.sizes[3] = d3;
            t.bdesc.DataType = DML_TENSOR_DATA_TYPE_FLOAT32;
            t.bdesc.Flags = DML_TENSOR_FLAG_NONE;
            t.bdesc.DimensionCount = 4;
            t.bdesc.Sizes = t.sizes;
            if (broadcast_w) {
                t.strides[0] = 0; t.strides[1] = 0; t.strides[2] = 0; t.strides[3] = 1;
                t.bdesc.Strides = t.strides;
                t.bdesc.TotalTensorSizeInBytes = static_cast<UINT64>(d3) * 4;
            } else {
                t.bdesc.Strides = nullptr;
                t.bdesc.TotalTensorSizeInBytes = static_cast<UINT64>(d0) * d1 * d2 * d3 * 4;
            }
            t.bdesc.GuaranteedBaseOffsetAlignment = 0;
            t.desc.Type = DML_TENSOR_TYPE_BUFFER;
            t.desc.Desc = &t.bdesc;
        };

        TensorSpec tx, tw1, tb1, ta1p, ts1, ta1;
        TensorSpec tw2, tb2, ta2p, ts2, ta2;
        TensorSpec tw3, tb3, ta3p, ts3, ta3;
        TensorSpec tgru_x, tgru_w, tgru_r, tgru_b, tgru_h0, tgru_hout;
        TensorSpec twout, tbout, tout;

        make_tensor(tx, 1, 1, batch, C::IN_DIM);
        make_tensor(tw1, 1, 1, C::IN_DIM, C::H1);
        make_tensor(tb1, 1, 1, batch, C::H1, true);
        make_tensor(ta1p, 1, 1, batch, C::H1);
        make_tensor(ts1, 1, 1, batch, C::H1);
        make_tensor(ta1, 1, 1, batch, C::H1);

        make_tensor(tw2, 1, 1, C::H1, C::H2);
        make_tensor(tb2, 1, 1, batch, C::H2, true);
        make_tensor(ta2p, 1, 1, batch, C::H2);
        make_tensor(ts2, 1, 1, batch, C::H2);
        make_tensor(ta2, 1, 1, batch, C::H2);

        make_tensor(tw3, 1, 1, C::H2, C::H3);
        make_tensor(tb3, 1, 1, batch, C::H3, true);
        make_tensor(ta3p, 1, 1, batch, C::H3);
        make_tensor(ts3, 1, 1, batch, C::H3);
        make_tensor(ta3, 1, 1, batch, C::H3);

        make_tensor(tgru_x, 1, 1, batch, C::H3);
        make_tensor(tgru_w, 1, 1, 3 * C::GRU_H, C::H3);
        make_tensor(tgru_r, 1, 1, 3 * C::GRU_H, C::GRU_H);
        make_tensor(tgru_b, 1, 1, 1, 6 * C::GRU_H);
        make_tensor(tgru_h0, 1, 1, batch, C::GRU_H);
        make_tensor(tgru_hout, 1, 1, batch, C::GRU_H);

        make_tensor(twout, 1, 1, C::GRU_H, C::OUT_DIM);
        make_tensor(tbout, 1, 1, batch, C::OUT_DIM, true);
        make_tensor(tout, 1, 1, batch, C::OUT_DIM);

        DML_GEMM_OPERATOR_DESC g1{ &tx.desc, &tw1.desc, &tb1.desc, &ta1p.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
        DML_ACTIVATION_SIGMOID_OPERATOR_DESC sig1{ &ta1p.desc, &ts1.desc };
        DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC mul1{ &ta1p.desc, &ts1.desc, &ta1.desc };

        DML_GEMM_OPERATOR_DESC g2{ &ta1.desc, &tw2.desc, &tb2.desc, &ta2p.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
        DML_ACTIVATION_SIGMOID_OPERATOR_DESC sig2{ &ta2p.desc, &ts2.desc };
        DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC mul2{ &ta2p.desc, &ts2.desc, &ta2.desc };

        DML_GEMM_OPERATOR_DESC g3{ &ta2.desc, &tw3.desc, &tb3.desc, &ta3p.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };
        DML_ACTIVATION_SIGMOID_OPERATOR_DESC sig3{ &ta3p.desc, &ts3.desc };
        DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC mul3{ &ta3p.desc, &ts3.desc, &ta3.desc };

        DML_ACTIVATION_SIGMOID_OPERATOR_DESC gru_sig{ nullptr, nullptr };
        DML_ACTIVATION_TANH_OPERATOR_DESC gru_tanh{ nullptr, nullptr };
        DML_OPERATOR_DESC gru_acts[2] = {
            { DML_OPERATOR_ACTIVATION_SIGMOID, &gru_sig },
            { DML_OPERATOR_ACTIVATION_TANH, &gru_tanh }
        };
        DML_GRU_OPERATOR_DESC gru_desc{
            &tgru_x.desc, &tgru_w.desc, &tgru_r.desc, &tgru_b.desc,
            &tgru_h0.desc, nullptr, nullptr, &tgru_hout.desc,
            2, gru_acts, DML_RECURRENT_NETWORK_DIRECTION_FORWARD, TRUE
        };

        DML_GEMM_OPERATOR_DESC g4{ &tgru_hout.desc, &twout.desc, &tbout.desc, &tout.desc, DML_MATRIX_TRANSFORM_NONE, DML_MATRIX_TRANSFORM_NONE, 1.0f, 1.0f, nullptr };

        DML_OPERATOR_DESC op_descs[NUM_OPS] = {
            { DML_OPERATOR_GEMM, &g1 },
            { DML_OPERATOR_ACTIVATION_SIGMOID, &sig1 },
            { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &mul1 },
            { DML_OPERATOR_GEMM, &g2 },
            { DML_OPERATOR_ACTIVATION_SIGMOID, &sig2 },
            { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &mul2 },
            { DML_OPERATOR_GEMM, &g3 },
            { DML_OPERATOR_ACTIVATION_SIGMOID, &sig3 },
            { DML_OPERATOR_ELEMENT_WISE_MULTIPLY, &mul3 },
            { DML_OPERATOR_GRU, &gru_desc },
            { DML_OPERATOR_GEMM, &g4 }
        };

        IDMLCompiledOperator* raw_ops[NUM_OPS] = {};
        for (int i = 0; i < NUM_OPS; ++i) {
            ComPtr<IDMLOperator> op;
            HRESULT hr = dml_device->CreateOperator(&op_descs[i], IID_PPV_ARGS(&op));
            if (FAILED(hr)) {
                std::printf("DML CreateOperator %d failed: 0x%08X\n", i, static_cast<unsigned>(hr));
                return false;
            }
            hr = dml_device->CompileOperator(op.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&ops[i]));
            if (FAILED(hr)) {
                std::printf("DML CompileOperator %d failed: 0x%08X\n", i, static_cast<unsigned>(hr));
                return false;
            }
            raw_ops[i] = ops[i].Get();
        }

        ComPtr<IDMLOperatorInitializer> initializer;
        if (FAILED(dml_device->CreateOperatorInitializer(NUM_OPS, raw_ops, IID_PPV_ARGS(&initializer)))) return false;

        DML_BINDING_PROPERTIES init_props = initializer->GetBindingProperties();
        UINT total_descs = init_props.RequiredDescriptorCount;
        UINT op_desc_offsets[NUM_OPS] = {};
        DML_BINDING_PROPERTIES exec_props[NUM_OPS] = {};
        uint64_t max_temp = init_props.TemporaryResourceSize;

        for (int i = 0; i < NUM_OPS; ++i) {
            exec_props[i] = ops[i]->GetBindingProperties();
            op_desc_offsets[i] = total_descs;
            total_descs += std::max<UINT>(exec_props[i].RequiredDescriptorCount, 1);
            max_temp = std::max(max_temp, exec_props[i].TemporaryResourceSize);
            if (exec_props[i].PersistentResourceSize > 0) {
                if (!make_uav(exec_props[i].PersistentResourceSize, op_persistent[i])) return false;
            }
        }
        if (max_temp > 0) {
            if (!make_uav(max_temp, shared_temp)) return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors = std::max<UINT>(total_descs, 1);
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(d3d_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&desc_heap)))) return false;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_base = desc_heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_base = desc_heap->GetGPUDescriptorHandleForHeapStart();

        DML_BINDING_TABLE_DESC init_btd{};
        init_btd.Dispatchable = initializer.Get();
        init_btd.CPUDescriptorHandle = cpu_base;
        init_btd.GPUDescriptorHandle = gpu_base;
        init_btd.SizeInDescriptors = std::max<UINT>(init_props.RequiredDescriptorCount, 1);
        ComPtr<IDMLBindingTable> init_bt;
        if (FAILED(dml_device->CreateBindingTable(&init_btd, IID_PPV_ARGS(&init_bt)))) return false;

        if (init_props.TemporaryResourceSize > 0) {
            DML_BUFFER_BINDING tb{ shared_temp.res.Get(), 0, init_props.TemporaryResourceSize };
            DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
            init_bt->BindTemporaryResource(&td);
        }
        DML_BUFFER_BINDING init_pbb[NUM_OPS] = {};
        DML_BINDING_DESC init_pbd[NUM_OPS] = {};
        for (int i = 0; i < NUM_OPS; ++i) {
            if (exec_props[i].PersistentResourceSize > 0) {
                init_pbb[i] = { op_persistent[i].res.Get(), 0, exec_props[i].PersistentResourceSize };
                init_pbd[i] = { DML_BINDING_TYPE_BUFFER, &init_pbb[i] };
            } else {
                init_pbd[i] = { DML_BINDING_TYPE_NONE, nullptr };
            }
        }
        init_bt->BindOutputs(NUM_OPS, init_pbd);

        cmd_alloc->Reset();
        cmd_list->Reset(cmd_alloc.Get(), nullptr);
        ID3D12DescriptorHeap* heaps[] = { desc_heap.Get() };
        cmd_list->SetDescriptorHeaps(1, heaps);
        cmd_recorder->RecordDispatch(cmd_list.Get(), initializer.Get(), init_bt.Get());
        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();

        auto mk_bt = [&](int idx) -> bool {
            DML_BINDING_TABLE_DESC btd{};
            btd.Dispatchable = ops[idx].Get();
            btd.CPUDescriptorHandle.ptr = cpu_base.ptr + static_cast<SIZE_T>(op_desc_offsets[idx]) * desc_inc_size;
            btd.GPUDescriptorHandle.ptr = gpu_base.ptr + static_cast<UINT64>(op_desc_offsets[idx]) * desc_inc_size;
            btd.SizeInDescriptors = std::max<UINT>(exec_props[idx].RequiredDescriptorCount, 1);
            if (FAILED(dml_device->CreateBindingTable(&btd, IID_PPV_ARGS(&op_bindings[idx])))) return false;
            if (exec_props[idx].TemporaryResourceSize > 0) {
                DML_BUFFER_BINDING tb{ shared_temp.res.Get(), 0, exec_props[idx].TemporaryResourceSize };
                DML_BINDING_DESC td{ DML_BINDING_TYPE_BUFFER, &tb };
                op_bindings[idx]->BindTemporaryResource(&td);
            }
            if (exec_props[idx].PersistentResourceSize > 0) {
                DML_BUFFER_BINDING pb{ op_persistent[idx].res.Get(), 0, exec_props[idx].PersistentResourceSize };
                DML_BINDING_DESC pd{ DML_BINDING_TYPE_BUFFER, &pb };
                op_bindings[idx]->BindPersistentResource(&pd);
            }
            return true;
        };
        for (int i = 0; i < NUM_OPS; ++i) {
            if (!mk_bt(i)) return false;
        }

        auto bind3 = [&](int idx, const DmlBuffer& a, uint64_t sa, const DmlBuffer& b, uint64_t sb, const DmlBuffer& c, uint64_t sc, const DmlBuffer& o, uint64_t so) {
            DML_BUFFER_BINDING ib[3] = { { a.res.Get(), 0, sa }, { b.res.Get(), 0, sb }, { c.res.Get(), 0, sc } };
            DML_BINDING_DESC id[3] = { { DML_BINDING_TYPE_BUFFER, &ib[0] }, { DML_BINDING_TYPE_BUFFER, &ib[1] }, { DML_BINDING_TYPE_BUFFER, &ib[2] } };
            op_bindings[idx]->BindInputs(3, id);
            DML_BUFFER_BINDING ob{ o.res.Get(), 0, so };
            DML_BINDING_DESC od{ DML_BINDING_TYPE_BUFFER, &ob };
            op_bindings[idx]->BindOutputs(1, &od);
        };
        auto bind1 = [&](int idx, const DmlBuffer& a, uint64_t sa, const DmlBuffer& o, uint64_t so) {
            DML_BUFFER_BINDING ib{ a.res.Get(), 0, sa };
            DML_BINDING_DESC id{ DML_BINDING_TYPE_BUFFER, &ib };
            op_bindings[idx]->BindInputs(1, &id);
            DML_BUFFER_BINDING ob{ o.res.Get(), 0, so };
            DML_BINDING_DESC od{ DML_BINDING_TYPE_BUFFER, &ob };
            op_bindings[idx]->BindOutputs(1, &od);
        };
        auto bind2 = [&](int idx, const DmlBuffer& a, uint64_t sa, const DmlBuffer& b, uint64_t sb, const DmlBuffer& o, uint64_t so) {
            DML_BUFFER_BINDING ib[2] = { { a.res.Get(), 0, sa }, { b.res.Get(), 0, sb } };
            DML_BINDING_DESC id[2] = { { DML_BINDING_TYPE_BUFFER, &ib[0] }, { DML_BINDING_TYPE_BUFFER, &ib[1] } };
            op_bindings[idx]->BindInputs(2, id);
            DML_BUFFER_BINDING ob{ o.res.Get(), 0, so };
            DML_BINDING_DESC od{ DML_BINDING_TYPE_BUFFER, &ob };
            op_bindings[idx]->BindOutputs(1, &od);
        };

        bind3(0, buf_x, batch * C::IN_DIM * 4, buf_w1, C::W1_SIZE * 4, buf_b1, C::B1_SIZE * 4, buf_a1p, batch * C::H1 * 4);
        bind1(1, buf_a1p, batch * C::H1 * 4, buf_s1, batch * C::H1 * 4);
        bind2(2, buf_a1p, batch * C::H1 * 4, buf_s1, batch * C::H1 * 4, buf_a1, batch * C::H1 * 4);

        bind3(3, buf_a1, batch * C::H1 * 4, buf_w2, C::W2_SIZE * 4, buf_b2, C::B2_SIZE * 4, buf_a2p, batch * C::H2 * 4);
        bind1(4, buf_a2p, batch * C::H2 * 4, buf_s2, batch * C::H2 * 4);
        bind2(5, buf_a2p, batch * C::H2 * 4, buf_s2, batch * C::H2 * 4, buf_a2, batch * C::H2 * 4);

        bind3(6, buf_a2, batch * C::H2 * 4, buf_w3, C::W3_SIZE * 4, buf_b3, C::B3_SIZE * 4, buf_a3p, batch * C::H3 * 4);
        bind1(7, buf_a3p, batch * C::H3 * 4, buf_s3, batch * C::H3 * 4);
        bind2(8, buf_a3p, batch * C::H3 * 4, buf_s3, batch * C::H3 * 4, buf_a3, batch * C::H3 * 4);

        DML_BUFFER_BINDING gru_ib[6] = {
            { buf_a3.res.Get(), 0, batch * C::H3 * 4 },
            { buf_gru_w.res.Get(), 0, C::W_IH_SIZE * 4 },
            { buf_gru_r.res.Get(), 0, C::W_HH_SIZE * 4 },
            { buf_gru_b.res.Get(), 0, (C::B_IH_SIZE + C::B_HH_SIZE) * 4 },
            { buf_h0.res.Get(), 0, batch * C::GRU_H * 4 },
            { nullptr, 0, 0 }
        };
        DML_BINDING_DESC gru_id[6] = {
            { DML_BINDING_TYPE_BUFFER, &gru_ib[0] },
            { DML_BINDING_TYPE_BUFFER, &gru_ib[1] },
            { DML_BINDING_TYPE_BUFFER, &gru_ib[2] },
            { DML_BINDING_TYPE_BUFFER, &gru_ib[3] },
            { DML_BINDING_TYPE_BUFFER, &gru_ib[4] },
            { DML_BINDING_TYPE_NONE, nullptr }
        };
        op_bindings[9]->BindInputs(6, gru_id);
        DML_BUFFER_BINDING gru_ob{ buf_h1.res.Get(), 0, batch * C::GRU_H * 4 };
        DML_BINDING_DESC gru_od[2] = {
            { DML_BINDING_TYPE_NONE, nullptr },
            { DML_BINDING_TYPE_BUFFER, &gru_ob }
        };
        op_bindings[9]->BindOutputs(2, gru_od);

        bind3(10, buf_h1, batch * C::GRU_H * 4, buf_wout, C::W_OUT_SIZE * 4, buf_bout, C::B_OUT_SIZE * 4, buf_out, batch * C::OUT_DIM * 4);

        prerecord_alloc->Reset();
        prerecord_list->Reset(prerecord_alloc.Get(), nullptr);
        record_all_ops(prerecord_list.Get());
        prerecord_list->Close();

        return true;
    }

    void record_all_ops(ID3D12GraphicsCommandList* cl) {
        ID3D12DescriptorHeap* heaps[] = { desc_heap.Get() };
        cl->SetDescriptorHeaps(1, heaps);
        D3D12_RESOURCE_BARRIER uav_barrier{};
        uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barrier.UAV.pResource = nullptr;
        for (int i = 0; i < NUM_OPS; ++i) {
            cmd_recorder->RecordDispatch(cl, ops[i].Get(), op_bindings[i].Get());
            if (i + 1 < NUM_OPS) {
                cl->ResourceBarrier(1, &uav_barrier);
            }
        }
    }

    void execute_recorded_fresh() {
        cmd_alloc->Reset();
        cmd_list->Reset(cmd_alloc.Get(), nullptr);
        record_all_ops(cmd_list.Get());
        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();
    }

    void execute_prerecorded() {
        ID3D12CommandList* lists[] = { prerecord_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();
    }

    double execute_gpu_timed() {
        if (!ts_heap || !ts_readback.res || ts_freq == 0) return -1.0;
        cmd_alloc->Reset();
        cmd_list->Reset(cmd_alloc.Get(), nullptr);
        cmd_list->EndQuery(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        record_all_ops(cmd_list.Get());
        cmd_list->EndQuery(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cmd_list->ResolveQueryData(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, ts_readback.res.Get(), 0);
        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        cmd_queue->ExecuteCommandLists(1, lists);
        wait_gpu();

        void* mapped = nullptr;
        D3D12_RANGE rr{ 0, 16 };
        if (FAILED(ts_readback.res->Map(0, &rr, &mapped))) return -1.0;
        uint64_t t[2] = {};
        std::memcpy(t, mapped, 16);
        D3D12_RANGE wr{ 0, 0 };
        ts_readback.res->Unmap(0, &wr);
        return (static_cast<double>(t[1] - t[0]) * 1e6) / static_cast<double>(ts_freq);
    }

    void destroy() {
        if (fence_event) { CloseHandle(fence_event); fence_event = nullptr; }
        ts_readback.res.Reset();
        ts_heap.Reset();
        upload_buf.res.Reset();
        readback_buf.res.Reset();
        shared_temp.res.Reset();
        for (int i = 0; i < NUM_OPS; ++i) {
            op_persistent[i].res.Reset();
            op_bindings[i].Reset();
            ops[i].Reset();
        }
        buf_x.res.Reset(); buf_h0.res.Reset();
        buf_w1.res.Reset(); buf_b1.res.Reset(); buf_a1p.res.Reset(); buf_s1.res.Reset(); buf_a1.res.Reset();
        buf_w2.res.Reset(); buf_b2.res.Reset(); buf_a2p.res.Reset(); buf_s2.res.Reset(); buf_a2.res.Reset();
        buf_w3.res.Reset(); buf_b3.res.Reset(); buf_a3p.res.Reset(); buf_s3.res.Reset(); buf_a3.res.Reset();
        buf_gru_w.res.Reset(); buf_gru_r.res.Reset(); buf_gru_b.res.Reset();
        buf_wout.res.Reset(); buf_bout.res.Reset();
        buf_out.res.Reset(); buf_h1.res.Reset();
        desc_heap.Reset();
        cmd_recorder.Reset();
        dml_device.Reset();
        fence.Reset();
        prerecord_list.Reset();
        prerecord_alloc.Reset();
        cmd_list.Reset();
        cmd_alloc.Reset();
        cmd_queue.Reset();
        d3d_device.Reset();
        api.unload();
    }
};

}
