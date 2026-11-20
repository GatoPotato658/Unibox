#pragma once

#include "strafenet_model.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <DirectML.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
            std::printf("DML CreateOperator type=%d failed: 0x%08X\n", static_cast<int>(desc.Type), static_cast<unsigned>(hr));
            return false;
        }
        OpBindingSpec spec{};
        hr = dml_device->CompileOperator(op.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&spec.compiled));
        if (FAILED(hr)) {
            std::printf("DML CompileOperator type=%d failed: 0x%08X\n", static_cast<int>(desc.Type), static_cast<unsigned>(hr));
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
            std::printf("  [DML dbg] CreateOperatorInitializer(%zu) failed: 0x%08X\n", raw_ops.size(), (unsigned)hr);
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
                    std::printf("  [DML dbg] make_uav persistent[%zu] (%llu) failed\n", i, (unsigned long long)pipe.ops[i].exec_props.PersistentResourceSize);
                    return false;
                }
            }
        }
        if (max_temp > 0) {
            if (!make_uav(max_temp, pipe.temp_buf)) {
                std::printf("  [DML dbg] make_uav temp (%llu) failed\n", (unsigned long long)max_temp);
                return false;
            }
        }

        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors = total_descs;
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(hr = d3d_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&pipe.heap)))) {
            std::printf("  [DML dbg] CreateDescriptorHeap(%u) failed: 0x%08X\n", total_descs, (unsigned)hr);
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cbase = pipe.heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gbase = pipe.heap->GetGPUDescriptorHandleForHeapStart();

        DML_BINDING_TABLE_DESC init_btd{ initializer.Get(), cbase, gbase, std::max<UINT>(init_props.RequiredDescriptorCount, 1) };
        ComPtr<IDMLBindingTable> init_bt;
        if (FAILED(hr = dml_device->CreateBindingTable(&init_btd, IID_PPV_ARGS(&init_bt)))) {
            std::printf("  [DML dbg] CreateBindingTable(init) failed: 0x%08X\n", (unsigned)hr);
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
            std::printf("  [DML dbg] DeviceRemoved after init dispatch: 0x%08X\n", (unsigned)hr);
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
                std::printf("  [DML dbg] CreateBindingTable(op=%zu) failed: 0x%08X (req=%u off=%u total=%u dml_rem=0x%08X)\n",
                    i, (unsigned)hr, op.exec_props.RequiredDescriptorCount, offsets[i], total_descs,
                    (unsigned)dml_device->GetDeviceRemovedReason());
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
            std::printf("  [DML dbg] alloc base failed (rem=0x%08X)\n", (unsigned)d3d_device->GetDeviceRemovedReason());
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

            if (!finalize_pipeline(pipe_fwd)) { std::printf("  [DML dbg] finalize pipe_fwd failed (rem=0x%08X)\n", (unsigned)d3d_device->GetDeviceRemovedReason()); return false; }
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

            if (!finalize_pipeline(pipe_bwd)) { std::printf("  [DML dbg] finalize pipe_bwd failed (rem=0x%08X)\n", (unsigned)d3d_device->GetDeviceRemovedReason()); return false; }
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

            if (!finalize_pipeline(pipe_adamw, false)) { std::printf("  [DML dbg] finalize pipe_adamw failed (rem=0x%08X)\n", (unsigned)d3d_device->GetDeviceRemovedReason()); return false; }

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
        upload_buffer(buf_xseq, x_seq, batch * D::SEQ_LEN * D::D_SEQ * 4);
        upload_buffer(buf_xctx, x_ctx, batch * D::D_CTX * 4);
        run_pipeline(pipe_fwd);
        download_buffer(buf_pred, out, batch * D::OUT_TOTAL * 4);
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
