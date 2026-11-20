#include "strafenet_model.hpp"
#include "strafenet_cpu.hpp"
#include "strafenet_cuda.hpp"
#include "strafenet_directml.hpp"
#include "neural_compute.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
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

void print_stats(const char* label, const BenchStats& st) {
    std::printf("  %-48s mean=%8.2f us  min=%8.2f us  p50=%8.2f us  p95=%8.2f us\n",
        label, st.mean_us, st.min_us, st.p50_us, st.p95_us);
}

void fill_det(std::vector<float>& v, uint32_t seed, float scale) {
    uint32_t s = seed;
    for (size_t i = 0; i < v.size(); ++i) {
        s = s * 1664525u + 1013904223u;
        int32_t x = static_cast<int32_t>(s >> 8) - 8388608;
        v[i] = static_cast<float>(x) * (scale / 8388608.0f);
    }
}

double eval_trunk_loss_f64(
    const strafenet::StrafeNetWeights& w,
    const strafenet::RefTrunkCache& cached_backbone,
    const float* target,
    uint32_t batch,
    float lambda_anchor,
    const std::vector<double>& theta_f64
) {
    using namespace strafenet;
    std::vector<double> a1(batch * ModelDims::TRUNK_H);
    std::vector<double> a2(batch * ModelDims::HIDDEN);
    std::vector<double> a3(batch * ModelDims::HIDDEN);
    std::vector<double> out(batch * ModelDims::OUT_TOTAL);

    auto silu_d = [](double x) -> double {
        return x / (1.0 + std::exp(-x));
    };
    for (uint32_t b = 0; b < batch; ++b) {
        for (uint32_t j = 0; j < ModelDims::TRUNK_H; ++j) {
            double acc = theta_f64[w.off_tr_b1 + j];
            for (uint32_t k = 0; k < ModelDims::TRUNK_H; ++k) {
                acc += static_cast<double>(cached_backbone.z0[b * ModelDims::TRUNK_H + k]) *
                       theta_f64[w.off_tr_w1 + k * ModelDims::TRUNK_H + j];
            }
            a1[b * ModelDims::TRUNK_H + j] = silu_d(acc);
        }
        for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) {
            double acc = theta_f64[w.off_tr_b2 + j];
            for (uint32_t k = 0; k < ModelDims::TRUNK_H; ++k) {
                acc += a1[b * ModelDims::TRUNK_H + k] * theta_f64[w.off_tr_w2 + k * ModelDims::HIDDEN + j];
            }
            a2[b * ModelDims::HIDDEN + j] = silu_d(acc);
        }
        for (uint32_t j = 0; j < ModelDims::HIDDEN; ++j) {
            double acc = theta_f64[w.off_tr_b3 + j];
            for (uint32_t k = 0; k < ModelDims::HIDDEN; ++k) {
                acc += a2[b * ModelDims::HIDDEN + k] * theta_f64[w.off_tr_w3 + k * ModelDims::HIDDEN + j];
            }
            a3[b * ModelDims::HIDDEN + j] = silu_d(acc);
        }
        for (uint32_t s = 0; s < ModelDims::OUT_STEPS; ++s) {
            for (uint32_t c = 0; c < ModelDims::OUT_DIMS; ++c) {
                uint32_t j = s * ModelDims::OUT_DIMS + c;
                double acc = theta_f64[w.off_tr_b4 + j];
                for (uint32_t k = 0; k < ModelDims::HIDDEN; ++k) {
                    acc += a3[b * ModelDims::HIDDEN + k] * theta_f64[w.off_tr_w4 + k * ModelDims::OUT_TOTAL + j];
                }
                double p = 0.0;
                if (c <= 2) {
                    p = std::tanh(acc);
                } else if (c == 3 || c == 4 || c == 7) {
                    p = 1.0 / (1.0 + std::exp(-acc));
                } else {
                    p = std::max(acc, 0.0) + std::log1p(std::exp(-std::fabs(acc))) + 1.0;
                }
                out[b * ModelDims::OUT_TOTAL + j] = p;
            }
        }
    }

    double mse = 0.0;
    for (size_t i = 0; i < out.size(); ++i) {
        double d = out[i] - static_cast<double>(target[i]);
        mse += 0.5 * d * d;
    }
    mse /= static_cast<double>(out.size());
    double l2 = 0.0;
    for (size_t i = 0; i < ModelDims::PLASTIC_TOTAL; ++i) {
        double d = theta_f64[i] - static_cast<double>(w.anchor_theta0[i]);
        l2 += 0.5 * static_cast<double>(lambda_anchor) * d * d;
    }
    return mse + l2;
}

}

int verify_snv1_mode(const char* snv1_path, const char* x_seq_path, const char* x_ctx_path, const char* ref_out_path) {
    using namespace strafenet;
    StrafeNetWeights w;
    if (!w.load_snv1(snv1_path)) {
        std::printf("Failed to load SNV1: %s\n", snv1_path);
        return 1;
    }
    auto read_bin = [](const char* path) -> std::vector<float> {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        auto sz = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<float> v(sz / sizeof(float));
        f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(sz));
        return v;
    };
    auto x_seq = read_bin(x_seq_path);
    auto x_ctx = read_bin(x_ctx_path);
    auto exp_out = read_bin(ref_out_path);
    if (x_seq.empty() || x_ctx.empty() || exp_out.empty()) {
        std::printf("Failed to read input/ref binaries\n");
        return 1;
    }
    uint32_t batch = static_cast<uint32_t>(x_ctx.size() / ModelDims::D_CTX);
    std::vector<float> out_ref(exp_out.size()), out_avx(exp_out.size()), out_cu(exp_out.size()), out_dml(exp_out.size());
    strafenet_cpu::CpuWorkspace ws;
    ref_forward(w, x_seq.data(), x_ctx.data(), out_ref.data(), batch);
    strafenet_cpu::avx2_forward(w, x_seq.data(), x_ctx.data(), out_avx.data(), batch, ws);

    strafenet_cuda::CudaBackend cu;
    float e_cu = -1.0f;
    if (cu.init(0) && cu.upload_model(w, std::max(batch, 64u))) {
        cu.forward_sync(w, x_seq.data(), x_ctx.data(), out_cu.data(), batch);
        e_cu = max_abs_diff(exp_out.data(), out_cu.data(), exp_out.size());
        cu.destroy();
    }

    strafenet_dml::DmlBackend dm;
    float e_dml = -1.0f;
    TrainConfig tcfg{};
    if (dm.init(-1) && dm.build_pipelines(w, batch, tcfg)) {
        dm.forward_sync(x_seq.data(), x_ctx.data(), out_dml.data(), batch);
        e_dml = max_abs_diff(exp_out.data(), out_dml.data(), exp_out.size());
        dm.destroy();
    }

    auto max_rel = [&](const std::vector<float>& a, const std::vector<float>& b) -> float {
        float m = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            float d = std::fabs(a[i] - b[i]) / std::max(1.0f, std::fabs(a[i]));
            if (d > m) m = d;
        }
        return m;
    };
    float e_ref = max_abs_diff(exp_out.data(), out_ref.data(), exp_out.size());
    float e_avx = max_abs_diff(exp_out.data(), out_avx.data(), exp_out.size());
    float r_ref = max_rel(exp_out, out_ref);
    float r_avx = max_rel(exp_out, out_avx);
    float r_cu = (e_cu >= 0.0f) ? max_rel(exp_out, out_cu) : -1.0f;
    float r_dml = (e_dml >= 0.0f) ? max_rel(exp_out, out_dml) : -1.0f;
    std::printf("SNV1 PyTorch Parity (arch=%u, batch=%u) max_abs: Scalar=%.3e AVX2=%.3e CUDA=%.3e DML=%.3e | max_rel: Scalar=%.3e AVX2=%.3e CUDA=%.3e DML=%.3e\n",
        static_cast<unsigned>(w.arch), batch, e_ref, e_avx, e_cu, e_dml, r_ref, r_avx, r_cu, r_dml);
    return (r_ref < 1e-5f && r_avx < 1e-5f && r_cu >= 0.0f && r_cu < 1e-5f && r_dml >= 0.0f && r_dml < 1e-5f) ? 0 : 2;
}

int main(int argc, char** argv) {
    using namespace strafenet;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 6 && std::strcmp(argv[1], "--verify-snv1") == 0) {
        return verify_snv1_mode(argv[2], argv[3], argv[4], argv[5]);
    }

    std::printf("=== StrafeNet v1 NeuralCompute Runtime & Continual Learning Verification ===\n");
    std::printf("Plastic Trunk Params: %u (%.2f KB) | TCN Backbone: %u | GRU Backbone: %u\n",
        ModelDims::PLASTIC_TOTAL, (ModelDims::PLASTIC_TOTAL * 4.0) / 1024.0,
        ModelDims::TCN_BACKBONE_TOTAL, ModelDims::GRU_BACKBONE_TOTAL);

    auto cpu_cap = strafenet_cpu::detect_cpu();
    std::printf("[CPU] %s | AVX2=%d FMA=%d OSXSAVE=%d | ActiveAVX2=%d\n\n",
        cpu_cap.brand, cpu_cap.avx2, cpu_cap.fma, cpu_cap.osxsave,
        strafenet_cpu::is_avx2_fma_supported() ? 1 : 0);

    std::printf("--- 1. SNV1 Binary Format Round-Trip ---\n");
    for (ArchType arch : { ArchType::TCN_ResMLP, ArchType::GRU_ResMLP }) {
        StrafeNetWeights w0, w1;
        w0.init_random(arch, arch == ArchType::TCN_ResMLP ? 1001u : 2002u);
        const char* fname = (arch == ArchType::TCN_ResMLP)
            ? "e:\\coding\\github\\Uniboxik\\ml_pipeline\\cpp_bench\\test_tcn.snv1"
            : "e:\\coding\\github\\Uniboxik\\ml_pipeline\\cpp_bench\\test_gru.snv1";
        bool ok_save = w0.save_snv1(fname);
        bool ok_load = w1.load_snv1(fname);
        float d_bb = max_abs_diff(w0.backbone.data(), w1.backbone.data(), w0.backbone.size());
        float d_th0 = max_abs_diff(w0.anchor_theta0.data(), w1.anchor_theta0.data(), w0.anchor_theta0.size());
        float d_th = max_abs_diff(w0.plastic_theta.data(), w1.plastic_theta.data(), w0.plastic_theta.size());
        std::printf("  arch=%u save=%d load=%d bb=%zu plastic=%zu diffs=(%.1e, %.1e, %.1e)\n",
            static_cast<unsigned>(arch), ok_save, ok_load,
            w1.backbone.size(), w1.plastic_theta.size(), d_bb, d_th0, d_th);
    }

    strafenet_cuda::CudaBackend cuda;
    bool has_cuda = cuda.init(0);
    if (has_cuda) {
        size_t free_b = 0, tot_b = 0;
        cuda.api.cuMemGetInfo_v2(&free_b, &tot_b);
        std::printf("\n[CUDA] %s | CC %d.%d | SMs=%d | VRAM=%.1f MiB (Free %.1f MiB)\n",
            cuda.info.name.c_str(), cuda.info.cc_major, cuda.info.cc_minor, cuda.info.sm_count,
            cuda.info.total_vram_bytes / (1024.0 * 1024.0), free_b / (1024.0 * 1024.0));
    }

    strafenet_dml::DmlBackend dml;
    bool has_dml = dml.init(-1);
    if (has_dml) {
        std::printf("[DirectML] %ls | Vendor=0x%04X Device=0x%04X | MaxFeatureLevel=0x%04X\n",
            dml.selected_adapter.description.c_str(),
            dml.selected_adapter.vendor_id, dml.selected_adapter.device_id,
            static_cast<unsigned>(dml.max_feature_level));
    }

    strafenet_cpu::CpuWorkspace cpu_ws;
    TrainConfig tcfg{};

    std::printf("\n--- 2. Forward Pass Numerical Verification (max_abs_err < 1e-4) ---\n");
    for (ArchType arch : { ArchType::TCN_ResMLP, ArchType::GRU_ResMLP }) {
        const char* aname = (arch == ArchType::TCN_ResMLP) ? "TCN_ResMLP" : "GRU_ResMLP";
        StrafeNetWeights w;
        w.init_random(arch, arch == ArchType::TCN_ResMLP ? 1001u : 2002u);
        if (has_cuda) cuda.upload_model(w, 64);

        for (uint32_t b : { 1u, 8u, 16u }) {
            std::vector<float> x_seq(b * ModelDims::SEQ_LEN * ModelDims::D_SEQ);
            std::vector<float> x_ctx(b * ModelDims::D_CTX);
            fill_det(x_seq, 300 + b, 1.0f);
            fill_det(x_ctx, 400 + b, 1.0f);

            std::vector<float> out_ref(b * ModelDims::OUT_TOTAL);
            std::vector<float> out_avx(b * ModelDims::OUT_TOTAL);
            std::vector<float> out_sse(b * ModelDims::OUT_TOTAL);
            std::vector<float> out_cu(b * ModelDims::OUT_TOTAL);
            std::vector<float> out_dml(b * ModelDims::OUT_TOTAL);

            ref_forward(w, x_seq.data(), x_ctx.data(), out_ref.data(), b);
            strafenet_cpu::avx2_forward(w, x_seq.data(), x_ctx.data(), out_avx.data(), b, cpu_ws);
            strafenet_cpu::sse2_forward(w, x_seq.data(), x_ctx.data(), out_sse.data(), b, cpu_ws);
            float e_avx = max_abs_diff(out_ref.data(), out_avx.data(), out_ref.size());
            float e_sse = max_abs_diff(out_ref.data(), out_sse.data(), out_ref.size());

            float e_cu = -1.0f;
            if (has_cuda) {
                cuda.forward_sync(w, x_seq.data(), x_ctx.data(), out_cu.data(), b);
                e_cu = max_abs_diff(out_ref.data(), out_cu.data(), out_ref.size());
            }
            float e_dml = -1.0f;
            if (has_dml) {
                if (!dml.build_pipelines(w, b, tcfg)) {
                    std::printf("  [ERROR] DML build_pipelines(%s, b=%u) failed!\n", aname, b);
                } else {
                    dml.forward_sync(x_seq.data(), x_ctx.data(), out_dml.data(), b);
                    e_dml = max_abs_diff(out_ref.data(), out_dml.data(), out_ref.size());
                }
            }
            std::printf("  %-10s b=%2u | AVX2=%.2e SSE2=%.2e CUDA=%.2e DML=%.2e\n",
                aname, b, e_avx, e_sse, e_cu, e_dml);
        }
    }

    std::printf("\n--- 3. Finite-Difference Numerical Gradient Check (eps=1e-3, rel_err < 1e-3) ---\n");
    {
        StrafeNetWeights w;
        w.init_random(ArchType::TCN_ResMLP, 5555u);
        const uint32_t b = 4;
        std::vector<float> x_seq(b * ModelDims::SEQ_LEN * ModelDims::D_SEQ);
        std::vector<float> x_ctx(b * ModelDims::D_CTX);
        std::vector<float> target(b * ModelDims::OUT_TOTAL);
        fill_det(x_seq, 701, 1.0f);
        fill_det(x_ctx, 702, 1.0f);
        fill_det(target, 703, 0.8f);

        RefTrunkCache cache;
        std::vector<float> out_ref(b * ModelDims::OUT_TOTAL);
        ref_forward(w, x_seq.data(), x_ctx.data(), out_ref.data(), b, &cache);
        std::vector<float> g_ref;
        ref_compute_trunk_grads(w, cache, target.data(), b, tcfg.lambda_anchor, g_ref);

        std::vector<float> g_avx(ModelDims::PLASTIC_TOTAL);
        {
            AdamWState opt_tmp;
            opt_tmp.reset();
            StrafeNetWeights w_tmp = w;
            strafenet_cpu::avx2_train_step(w_tmp, opt_tmp, x_seq.data(), x_ctx.data(), target.data(), b, tcfg, cpu_ws);
            g_avx = cpu_ws.grads;
        }

        std::vector<float> g_cu(ModelDims::PLASTIC_TOTAL);
        if (has_cuda) {
            cuda.upload_model(w, 64);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_xseq, x_seq.data(), x_seq.size() * 4);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_xctx, x_ctx.data(), x_ctx.size() * 4);
            cuda.api.cuMemcpyHtoD_v2(cuda.d_target, target.data(), target.size() * 4);
            cuda.forward_async(w, b);
            cuda.backward_and_adamw_async(w, b, tcfg, 10.0f, 1000.0f);
            cuda.api.cuStreamSynchronize(cuda.stream);
            cuda.api.cuMemcpyDtoH_v2(g_cu.data(), cuda.d_grads, ModelDims::PLASTIC_TOTAL * 4);
        }

        std::vector<float> g_dml(ModelDims::PLASTIC_TOTAL);
        if (has_dml && dml.build_pipelines(w, b, tcfg)) {
            dml.upload_buffer(dml.buf_xseq, x_seq.data(), x_seq.size() * 4);
            dml.upload_buffer(dml.buf_xctx, x_ctx.data(), x_ctx.size() * 4);
            dml.upload_buffer(dml.buf_target, target.data(), target.size() * 4);
            dml.run_pipeline(dml.pipe_fwd);
            dml.run_pipeline(dml.pipe_bwd);
            dml.download_buffer(dml.buf_grads, g_dml.data(), ModelDims::PLASTIC_TOTAL * 4);
        } else {
            has_dml = false;
        }

        std::printf("  Grad tensor max_abs_diff vs Ref: AVX2=%.2e | CUDA=%.2e | DML=%.2e\n",
            max_abs_diff(g_ref.data(), g_avx.data(), g_ref.size()),
            has_cuda ? max_abs_diff(g_ref.data(), g_cu.data(), g_ref.size()) : -1.0f,
            has_dml ? max_abs_diff(g_ref.data(), g_dml.data(), g_ref.size()) : -1.0f);

        size_t probe_indices[] = {
            w.off_tr_w1 + 0, w.off_tr_w1 + 12345, w.off_tr_b1 + 17,
            w.off_tr_w2 + 777, w.off_tr_b2 + 63,
            w.off_tr_w3 + 512, w.off_tr_b3 + 42,
            w.off_tr_w4 + 512, w.off_tr_w4 + 8197, w.off_tr_b4 + 5, w.off_tr_b4 + 6, w.off_tr_b4 + 7
        };
        const double eps_fd = 1e-3;
        std::vector<double> theta_f64(ModelDims::PLASTIC_TOTAL);
        for (size_t i = 0; i < ModelDims::PLASTIC_TOTAL; ++i) theta_f64[i] = static_cast<double>(w.plastic_theta[i]);
        double max_rel_err = 0.0;
        for (size_t idx : probe_indices) {
            double orig = theta_f64[idx];
            theta_f64[idx] = orig + eps_fd;
            double lp = eval_trunk_loss_f64(w, cache, target.data(), b, tcfg.lambda_anchor, theta_f64);
            theta_f64[idx] = orig - eps_fd;
            double lm = eval_trunk_loss_f64(w, cache, target.data(), b, tcfg.lambda_anchor, theta_f64);
            theta_f64[idx] = orig;

            double fd = (lp - lm) / (2.0 * eps_fd);
            double an = g_ref[idx];
            double rel = std::fabs(fd - an) / std::max({ std::fabs(fd), std::fabs(an), 1e-6 });
            if (rel > max_rel_err) max_rel_err = rel;
            std::printf("    idx=%6zu: FD=%+11.7f  Ref=%+11.7f  CUDA=%+11.7f  DML=%+11.7f  rel=%.2e\n",
                idx, fd, an, has_cuda ? g_cu[idx] : 0.0f, has_dml ? g_dml[idx] : 0.0f, rel);
        }
        std::printf("  Max Finite-Diff relative error = %.3e\n", max_rel_err);
    }

    std::printf("\n--- 4. Multi-Step Online Continual Learning, Norm Clipping & NaN Guard ---\n");
    {
        StrafeNetWeights w_ref, w_avx, w_cu, w_dml;
        w_ref.init_random(ArchType::TCN_ResMLP, 8888u);
        w_avx = w_ref; w_cu = w_ref; w_dml = w_ref;
        AdamWState opt_ref, opt_avx;
        opt_ref.reset(); opt_avx.reset();

        const uint32_t b = 32;
        std::vector<float> x_seq(b * ModelDims::SEQ_LEN * ModelDims::D_SEQ);
        std::vector<float> x_ctx(b * ModelDims::D_CTX);
        std::vector<float> target(b * ModelDims::OUT_TOTAL);
        fill_det(x_seq, 801, 1.0f);
        fill_det(x_ctx, 802, 1.0f);
        fill_det(target, 803, 0.7f);

        if (has_cuda) cuda.upload_model(w_cu, 64);
        if (has_dml) dml.build_pipelines(w_dml, b, tcfg);

        for (int s = 0; s < 5; ++s) {
            auto mr = ref_train_step(w_ref, opt_ref, x_seq.data(), x_ctx.data(), target.data(), b, tcfg);
            auto ma = strafenet_cpu::avx2_train_step(w_avx, opt_avx, x_seq.data(), x_ctx.data(), target.data(), b, tcfg, cpu_ws);
            TrainStepMetrics mc{}, md{};
            if (has_cuda) mc = cuda.train_step_sync(w_cu, x_seq.data(), x_ctx.data(), target.data(), b, tcfg);
            if (has_dml) md = dml.train_step_sync(x_seq.data(), x_ctx.data(), target.data(), b, tcfg);
            float d1 = max_abs_diff(w_ref.plastic_theta.data(), w_avx.plastic_theta.data(), ModelDims::PLASTIC_TOTAL);
            std::printf("  step %d: loss_ref=%.6f (mse=%.6f l2=%.6f) loss_avx=%.6f (mse=%.6f l2=%.6f) loss_cu=%.6f loss_dml=%.6f | gnorm=%.5f | avx_diff=%.2e\n",
                s + 1, mr.total_loss, mr.mse_loss, mr.l2_anchor_loss, ma.total_loss, ma.mse_loss, ma.l2_anchor_loss, mc.total_loss, md.total_loss, mr.grad_norm, d1);
        }
        if (has_cuda) cuda.download_plastic_weights(w_cu);
        if (has_dml) dml.download_plastic_weights(w_dml);
        std::printf("  5-step theta max_abs_err vs Ref: AVX2=%.2e | CUDA=%.2e | DML=%.2e\n",
            max_abs_diff(w_ref.plastic_theta.data(), w_avx.plastic_theta.data(), ModelDims::PLASTIC_TOTAL),
            has_cuda ? max_abs_diff(w_ref.plastic_theta.data(), w_cu.plastic_theta.data(), ModelDims::PLASTIC_TOTAL) : -1.0f,
            has_dml ? max_abs_diff(w_ref.plastic_theta.data(), w_dml.plastic_theta.data(), ModelDims::PLASTIC_TOTAL) : -1.0f);

        std::vector<float> bad_target = target;
        bad_target[0] = std::numeric_limits<float>::quiet_NaN();
        std::vector<float> saved_cu = w_cu.plastic_theta;
        std::vector<float> saved_dml = w_dml.plastic_theta;
        std::vector<float> saved_avx = w_avx.plastic_theta;

        auto m_bad_avx = strafenet_cpu::avx2_train_step(w_avx, opt_avx, x_seq.data(), x_ctx.data(), bad_target.data(), b, tcfg, cpu_ws);
        TrainStepMetrics m_bad_cu{}, m_bad_dml{};
        if (has_cuda) {
            m_bad_cu = cuda.train_step_sync(w_cu, x_seq.data(), x_ctx.data(), bad_target.data(), b, tcfg);
            cuda.download_plastic_weights(w_cu);
        }
        if (has_dml) {
            m_bad_dml = dml.train_step_sync(x_seq.data(), x_ctx.data(), bad_target.data(), b, tcfg);
            dml.download_plastic_weights(w_dml);
        }
        std::printf("  NaN Guard: AVX2 applied=%d diff=%.1e | CUDA applied=%d diff=%.1e | DML applied=%d diff=%.1e\n",
            m_bad_avx.applied ? 1 : 0, max_abs_diff(saved_avx.data(), w_avx.plastic_theta.data(), ModelDims::PLASTIC_TOTAL),
            m_bad_cu.applied ? 1 : 0, has_cuda ? max_abs_diff(saved_cu.data(), w_cu.plastic_theta.data(), ModelDims::PLASTIC_TOTAL) : -1.0f,
            m_bad_dml.applied ? 1 : 0, has_dml ? max_abs_diff(saved_dml.data(), w_dml.plastic_theta.data(), ModelDims::PLASTIC_TOTAL) : -1.0f);

        std::vector<float> huge_target = target;
        for (float& v : huge_target) v *= 1000.0f;
        auto m_clip_ref = ref_train_step(w_ref, opt_ref, x_seq.data(), x_ctx.data(), huge_target.data(), b, tcfg);
        auto m_clip_avx = strafenet_cpu::avx2_train_step(w_avx, opt_avx, x_seq.data(), x_ctx.data(), huge_target.data(), b, tcfg, cpu_ws);
        TrainStepMetrics m_clip_cu{}, m_clip_dml{};
        if (has_cuda) {
            m_clip_cu = cuda.train_step_sync(w_cu, x_seq.data(), x_ctx.data(), huge_target.data(), b, tcfg);
            cuda.download_plastic_weights(w_cu);
        }
        if (has_dml) {
            m_clip_dml = dml.train_step_sync(x_seq.data(), x_ctx.data(), huge_target.data(), b, tcfg);
            dml.download_plastic_weights(w_dml);
        }
        std::printf("  Clip Check (max_norm=1.0): gnorm=%.3f | applied=(%d,%d,%d) | theta_diff(AVX2=%.2e CUDA=%.2e DML=%.2e)\n",
            m_clip_ref.grad_norm, m_clip_avx.applied ? 1 : 0, m_clip_cu.applied ? 1 : 0, m_clip_dml.applied ? 1 : 0,
            max_abs_diff(w_ref.plastic_theta.data(), w_avx.plastic_theta.data(), ModelDims::PLASTIC_TOTAL),
            has_cuda ? max_abs_diff(w_ref.plastic_theta.data(), w_cu.plastic_theta.data(), ModelDims::PLASTIC_TOTAL) : -1.0f,
            has_dml ? max_abs_diff(w_ref.plastic_theta.data(), w_dml.plastic_theta.data(), ModelDims::PLASTIC_TOTAL) : -1.0f);
    }

    if (has_cuda) cuda.destroy();
    if (has_dml) dml.destroy();

    std::printf("\n--- 5. ComputeDispatcher Auto Hybrid Routing ---\n");
    {
        strafenet_rt::ComputeDispatcher rt;
        rt.init(strafenet_rt::BackendKind::Auto, 64);
        StrafeNetWeights w;
        w.init_random(ArchType::TCN_ResMLP, 1234u);
        rt.load_weights(w, 16);

        std::vector<float> x_seq16(16 * ModelDims::SEQ_LEN * ModelDims::D_SEQ);
        std::vector<float> x_ctx16(16 * ModelDims::D_CTX);
        std::vector<float> out16(16 * ModelDims::OUT_TOTAL);
        std::vector<float> tgt16(16 * ModelDims::OUT_TOTAL);
        fill_det(x_seq16, 11, 1.0f);
        fill_det(x_ctx16, 12, 1.0f);
        fill_det(tgt16, 13, 0.5f);

        auto b1 = rt.forward(x_seq16.data(), x_ctx16.data(), out16.data(), 1);
        auto b16 = rt.forward(x_seq16.data(), x_ctx16.data(), out16.data(), 16);
        strafenet_rt::BackendKind btrain = strafenet_rt::BackendKind::Auto;
        auto mtr = rt.train_step(x_seq16.data(), x_ctx16.data(), tgt16.data(), 16, &btrain, true);
        std::printf("  Auto route: b=1 fwd -> %s | b=16 fwd -> %s | b=16 train -> %s (applied=%d, loss=%.5f)\n",
            strafenet_rt::backend_name(b1), strafenet_rt::backend_name(b16),
            strafenet_rt::backend_name(btrain), mtr.applied ? 1 : 0, mtr.total_loss);
        rt.destroy();
    }

    std::printf("\n--- 6. StrafeNet v1 Full Benchmarks (us) ---\n");
    {
        strafenet_cuda::CudaBackend cu;
        bool ok_cu = cu.init(0);
        strafenet_dml::DmlBackend dm;
        bool ok_dm = dm.init(-1);

        for (ArchType arch : { ArchType::TCN_ResMLP, ArchType::GRU_ResMLP }) {
            const char* aname = (arch == ArchType::TCN_ResMLP) ? "TCN_ResMLP" : "GRU_ResMLP";
            std::printf("\n[%s]\n", aname);
            StrafeNetWeights w;
            w.init_random(arch, 4242u);
            if (ok_cu) cu.upload_model(w, 64);

            for (uint32_t b : { 1u, 8u, 16u }) {
                std::vector<float> x_seq(b * ModelDims::SEQ_LEN * ModelDims::D_SEQ);
                std::vector<float> x_ctx(b * ModelDims::D_CTX);
                std::vector<float> out(b * ModelDims::OUT_TOTAL);
                fill_det(x_seq, 50 + b, 1.0f);
                fill_det(x_ctx, 60 + b, 1.0f);

                char lbl[96];
                std::snprintf(lbl, sizeof(lbl), "CPU AVX2+FMA Forward (b=%u)", b);
                print_stats(lbl, bench_fn(100, 1000, [&]() {
                    strafenet_cpu::avx2_forward(w, x_seq.data(), x_ctx.data(), out.data(), b, cpu_ws);
                }));

                if (b == 1) {
                    std::snprintf(lbl, sizeof(lbl), "CPU SSE2 Fallback Forward (b=%u)", b);
                    print_stats(lbl, bench_fn(30, 200, [&]() {
                        strafenet_cpu::sse2_forward(w, x_seq.data(), x_ctx.data(), out.data(), b, cpu_ws);
                    }));
                }

                if (ok_cu) {
                    cu.api.cuMemcpyHtoD_v2(cu.d_xseq, x_seq.data(), x_seq.size() * 4);
                    cu.api.cuMemcpyHtoD_v2(cu.d_xctx, x_ctx.data(), x_ctx.size() * 4);
                    std::snprintf(lbl, sizeof(lbl), "CUDA Kernel Sync Forward (b=%u)", b);
                    print_stats(lbl, bench_fn(50, 300, [&]() {
                        cu.forward_async(w, b);
                        cu.api.cuStreamSynchronize(cu.stream);
                    }));
                    std::snprintf(lbl, sizeof(lbl), "CUDA HtoD+Fwd+DtoH Sync (b=%u)", b);
                    print_stats(lbl, bench_fn(50, 300, [&]() {
                        cu.forward_sync(w, x_seq.data(), x_ctx.data(), out.data(), b);
                    }));
                }

                if (ok_dm && dm.build_pipelines(w, b, tcfg)) {
                    dm.upload_buffer(dm.buf_xseq, x_seq.data(), x_seq.size() * 4);
                    dm.upload_buffer(dm.buf_xctx, x_ctx.data(), x_ctx.size() * 4);
                    std::snprintf(lbl, sizeof(lbl), "DML PreRecorded Exec+Wait Forward (b=%u)", b);
                    print_stats(lbl, bench_fn(30, 150, [&]() {
                        dm.run_pipeline(dm.pipe_fwd);
                    }));
                    std::snprintf(lbl, sizeof(lbl), "DML Upload+Exec+Readback Forward (b=%u)", b);
                    print_stats(lbl, bench_fn(20, 100, [&]() {
                        dm.forward_sync(x_seq.data(), x_ctx.data(), out.data(), b);
                    }));
                } else if (ok_dm) {
                    ok_dm = false;
                }
            }
        }

        std::printf("\n[Online Continual Learning Step: Forward + Trunk Backprop + L2 + Clip + AdamW]\n");
        StrafeNetWeights w_tr;
        w_tr.init_random(ArchType::TCN_ResMLP, 9001u);
        if (ok_cu) {
            cu.upload_model(w_tr, 64);
            std::printf("  CUDA allocated VRAM: %.2f MiB\n", cu.allocated_bytes / (1024.0 * 1024.0));
        }

        for (uint32_t b : { 32u, 64u }) {
            std::vector<float> x_seq(b * ModelDims::SEQ_LEN * ModelDims::D_SEQ);
            std::vector<float> x_ctx(b * ModelDims::D_CTX);
            std::vector<float> target(b * ModelDims::OUT_TOTAL);
            fill_det(x_seq, 91 + b, 1.0f);
            fill_det(x_ctx, 92 + b, 1.0f);
            fill_det(target, 93 + b, 0.5f);

            AdamWState opt_cpu;
            opt_cpu.reset();
            char lbl[96];
            std::snprintf(lbl, sizeof(lbl), "CPU AVX2+FMA Train Step (b=%u)", b);
            print_stats(lbl, bench_fn(20, 150, [&]() {
                strafenet_cpu::avx2_train_step(w_tr, opt_cpu, x_seq.data(), x_ctx.data(), target.data(), b, tcfg, cpu_ws);
            }));

            if (ok_cu) {
                std::snprintf(lbl, sizeof(lbl), "CUDA HtoD+Train+Sync (b=%u)", b);
                print_stats(lbl, bench_fn(30, 200, [&]() {
                    cu.train_step_sync(w_tr, x_seq.data(), x_ctx.data(), target.data(), b, tcfg);
                }));
            }

            if (ok_dm && dm.build_pipelines(w_tr, b, tcfg)) {
                if (b == 64) {
                    std::printf("  DirectML (b=64) allocated VRAM: %.2f MiB\n", dm.gpu_bytes_allocated / (1024.0 * 1024.0));
                }
                std::snprintf(lbl, sizeof(lbl), "DML Upload+Train+Sync (b=%u)", b);
                print_stats(lbl, bench_fn(15, 60, [&]() {
                    dm.train_step_sync(x_seq.data(), x_ctx.data(), target.data(), b, tcfg);
                }));
            }
        }

        if (ok_cu) cu.destroy();
        if (ok_dm) dm.destroy();
    }

    return 0;
}
