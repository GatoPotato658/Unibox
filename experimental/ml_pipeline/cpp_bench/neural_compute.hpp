#pragma once

#include "strafenet_model.hpp"
#include "strafenet_cpu.hpp"
#include "strafenet_cuda.hpp"
#include "strafenet_directml.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

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
            if (!cuda.upload_model(weights, gpu_batch_cap)) return false;
        }
        if (has_dml) {
            if (!dml.build_pipelines(weights, dml_batch, train_cfg)) return false;
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
        if (batch == 1 && has_avx2) {
            return BackendKind::CpuAvx2;
        }
        if (batch >= 8 && (has_cuda || has_dml)) {
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
        if (b == BackendKind::Cuda && has_cuda) {
            cuda.forward_sync(weights, x_seq, x_ctx, out, batch);
            return BackendKind::Cuda;
        }
        if (b == BackendKind::DirectML && has_dml) {
            if (dml.active_batch != batch || dml.active_arch != weights.arch) {
                dml.build_pipelines(weights, batch, train_cfg);
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

    void destroy() {
        if (has_cuda) { cuda.destroy(); has_cuda = false; }
        if (has_dml) { dml.destroy(); has_dml = false; }
    }
};

}
