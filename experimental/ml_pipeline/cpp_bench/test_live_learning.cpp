#include "../../Unibox/src/Features/Simulation/NeuralPrediction/NeuralCompute.h"
#include "../../Unibox/src/Features/Simulation/NeuralPrediction/NeuralWeightsEmbedded.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct ExtractedFrameRecord {
    uint64_t steam_id;
    int32_t demo_tick;
    int32_t server_tick;
    float origin[3];
    float velocity[3];
    float eye_angles[2];
    float wish_fwd;
    float wish_side;
    float maxspeed;
    float sim_time;
    float ground_dist;
    float ground_normal[3];
    float ceiling_dist;
    float wall_dists[8];
    float ledge_depths[8];
    uint32_t player_cond[5];
    uint32_t flags;
    uint32_t buttons;
    int32_t active_weapon;
    uint16_t entity_id;
    int16_t health;
    int16_t max_health;
    uint8_t team;
    uint8_t class_id;
    uint8_t water_level;
    uint8_t air_dash;
    uint8_t air_ducked;
    uint8_t jumping;
    uint8_t vel_source;
    uint8_t input_source;
    uint8_t wall_contact;
    uint8_t pad[5];
};
#pragma pack(pop)
static_assert(sizeof(ExtractedFrameRecord) == 200, "FrameRecord must be 200 bytes");

namespace {

constexpr int kSeqLen = 32;
constexpr int kSeqDim = 22;
constexpr int kCtxDim = 44;
constexpr int kOutSteps = 16;
constexpr int kOutDims = 8;
constexpr int kKnots[16] = { 1, 2, 4, 6, 9, 12, 16, 21, 27, 34, 42, 52, 64, 78, 96, 120 };
constexpr float kPi = 3.14159265358979323846f;

inline float Deg2Rad(float d) { return d * (kPi / 180.f); }
inline float WrapDeg(float a) {
    float r = std::fmod(a + 180.f, 360.f);
    if (r < 0.f) r += 360.f;
    return r - 180.f;
}
inline bool HasCond(const uint32_t cond[5], int bit) {
    return ((cond[bit / 32] >> (bit % 32)) & 1u) != 0;
}

float ClassDefaultMaxSpeed(uint8_t c) {
    switch (c) {
    case 1: return 400.f;
    case 2: return 300.f;
    case 3: return 240.f;
    case 4: return 280.f;
    case 5: return 320.f;
    case 6: return 230.f;
    case 7: return 300.f;
    case 8: return 320.f;
    case 9: return 300.f;
    default: return 300.f;
    }
}

void ApplyFriction2D(float& vx, float& vy, float dt) {
    float sp = std::hypot(vx, vy);
    if (sp < 0.1f) return;
    float ctrl = std::max(sp, 100.f);
    float drop = ctrl * 4.f * dt;
    float nsp = std::max(sp - drop, 0.f);
    if (nsp != sp) {
        float s = nsp / sp;
        vx *= s; vy *= s;
    }
}

void StepGround2D(float& vx, float& vy, float wx, float wy, float wsp, float maxsp, float dt, bool lost_footing) {
    ApplyFriction2D(vx, vy, dt);
    if (wsp > 0.f) {
        float cur = vx * wx + vy * wy;
        float add = wsp - cur;
        if (add > 0.f) {
            float accel = 10.f;
            float thresh = 100.f * 4.f / 10.f;
            if (wsp < thresh && !lost_footing) {
                float sp = std::hypot(vx, vy);
                float ctrl = std::max(sp, 100.f);
                accel = (ctrl * 4.f) / wsp + 1.f;
            }
            float gain = std::min(accel * wsp * dt, add);
            vx += wx * gain; vy += wy * gain;
        }
    }
    float sp = std::hypot(vx, vy);
    if (maxsp > 0.f && sp > maxsp) {
        float s = maxsp / sp;
        vx *= s; vy *= s;
    }
}

void StepAir2D(float& vx, float& vy, float wx, float wy, float wsp, float dt) {
    if (wsp <= 0.f) return;
    float cap = std::min(wsp, 30.f);
    float cur = vx * wx + vy * wy;
    float add = cap - cur;
    if (add <= 0.f) return;
    float gain = std::min(10.f * wsp * dt, add);
    vx += wx * gain; vy += wy * gain;
}

void InferWishDir(const ExtractedFrameRecord& prev, const ExtractedFrameRecord& curr, float& out_f, float& out_s) {
    int n_ticks = std::clamp(curr.server_tick - prev.server_tick, 1, 4);
    bool on_g = (prev.flags & 1u) != 0;
    bool duck = (prev.flags & 2u) != 0;
    bool lost = HasCond(prev.player_cond, 126);
    float base_max = prev.maxspeed > 10.f ? prev.maxspeed : ClassDefaultMaxSpeed(prev.class_id);
    float eff_max = (on_g && duck) ? (base_max / 3.f) : base_max;
    float rad = Deg2Rad(prev.eye_angles[1]);
    float c = std::cos(rad), s = std::sin(rad);

    float ivx = prev.velocity[0], ivy = prev.velocity[1];
    if (on_g) for (int i = 0; i < n_ticks; ++i) ApplyFriction2D(ivx, ivy, 0.015f);
    float dvx = curr.velocity[0] - ivx, dvy = curr.velocity[1] - ivy;
    float dvm = std::hypot(dvx, dvy);

    static constexpr float kCands[9][2] = {
        {0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
    };
    int best = 0;
    float best_e = 1e30f, zero_e = 1e30f;
    for (int i = 0; i < 9; ++i) {
        float wf = kCands[i][0], ws = kCands[i][1];
        float wx = c * wf + s * ws, wy = s * wf - c * ws;
        float wl = std::hypot(wx, wy);
        float dx = 0, dy = 0, wsp = 0;
        if (wl > 1e-6f) { dx = wx / wl; dy = wy / wl; wsp = eff_max; }
        float vx = prev.velocity[0], vy = prev.velocity[1];
        for (int t = 0; t < n_ticks; ++t) {
            if (on_g) StepGround2D(vx, vy, dx, dy, wsp, eff_max, 0.015f, lost);
            else StepAir2D(vx, vy, dx, dy, wsp, 0.015f);
        }
        float ex = vx - curr.velocity[0], ey = vy - curr.velocity[1];
        float e2 = ex * ex + ey * ey;
        if (i == 0) zero_e = e2;
        if (e2 < best_e) { best_e = e2; best = i; }
    }
    if (best == 0) {
        float v1m = std::hypot(curr.velocity[0], curr.velocity[1]);
        float v0m = std::hypot(prev.velocity[0], prev.velocity[1]);
        if (on_g && v1m > eff_max * 0.92f && std::fabs(v1m - v0m) < 2.f) {
            float dx = curr.velocity[0] / v1m, dy = curr.velocity[1] / v1m;
            out_f = std::clamp(dx * c + dy * s, -1.f, 1.f);
            out_s = std::clamp(dx * s - dy * c, -1.f, 1.f);
            return;
        }
        out_f = 0.f; out_s = 0.f; return;
    }
    if ((zero_e - best_e) < 0.25f && dvm < 1.5f) { out_f = 0.f; out_s = 0.f; return; }
    if (dvm >= 2.f && dvm <= eff_max * 10.f * 0.015f * float(n_ticks) * 1.1f) {
        float cdx = dvx / dvm, cdy = dvy / dvm;
        float vx = prev.velocity[0], vy = prev.velocity[1];
        for (int t = 0; t < n_ticks; ++t) {
            if (on_g) StepGround2D(vx, vy, cdx, cdy, eff_max, eff_max, 0.015f, lost);
            else StepAir2D(vx, vy, cdx, cdy, eff_max, 0.015f);
        }
        float ex = vx - curr.velocity[0], ey = vy - curr.velocity[1];
        if (ex * ex + ey * ey < best_e * 0.6f) {
            float pf = cdx * c + cdy * s, ps = cdx * s - cdy * c;
            float mag = std::max(std::hypot(pf, ps), 1e-6f);
            out_f = pf / mag; out_s = ps / mag; return;
        }
    }
    float bf = kCands[best][0], bs = kCands[best][1];
    float mag = std::hypot(bf, bs);
    out_f = bf / mag; out_s = bs / mag;
}

float CountRev(const float* v, int n, float th) {
    int last = 0, cnt = 0;
    for (int i = 0; i < n; ++i) {
        if (std::fabs(v[i]) <= th) continue;
        int sg = v[i] > 0.f ? 1 : -1;
        if (last != 0 && sg != last) ++cnt;
        last = sg;
    }
    return float(cnt);
}

void PackWindowFeatures(const std::vector<ExtractedFrameRecord>& hist, const std::vector<std::pair<float, float>>& inferred_wish, float* x_seq, float* x_ctx) {
    const auto& anchor = hist[kSeqLen - 1];
    float psi0_deg = anchor.eye_angles[1];
    float psi0 = Deg2Rad(psi0_deg);
    float c0 = std::cos(psi0), s0 = std::sin(psi0);

    float vf[kSeqLen], vr[kSeqLen], vz[kSeqLen], sp2d[kSeqLen], maxsp[kSeqLen], wf[kSeqLen], ws[kSeqLen], on_g[kSeqLen], dyaw_rate[kSeqLen];
    for (int t = 0; t < kSeqLen; ++t) {
        const auto& f = hist[t];
        vf[t] = f.velocity[0] * c0 + f.velocity[1] * s0;
        vr[t] = f.velocity[0] * s0 - f.velocity[1] * c0;
        vz[t] = f.velocity[2];
        sp2d[t] = std::hypot(f.velocity[0], f.velocity[1]);
        maxsp[t] = std::max(f.maxspeed, 100.f);
        wf[t] = std::clamp(inferred_wish[t].first, -1.f, 1.f);
        ws[t] = std::clamp(inferred_wish[t].second, -1.f, 1.f);
        on_g[t] = (f.flags & 1u) ? 1.f : 0.f;
    }
    std::memset(x_seq, 0, kSeqLen * kSeqDim * sizeof(float));
    for (int t = 0; t < kSeqLen; ++t) {
        const auto& f = hist[t];
        float dvf = t > 0 ? vf[t] - vf[t - 1] : 0.f;
        float dvr = t > 0 ? vr[t] - vr[t - 1] : 0.f;
        float dvz = t > 0 ? vz[t] - vz[t - 1] : 0.f;
        float dt_f = t > 0 ? float(std::max(f.server_tick - hist[t - 1].server_tick, 1)) : 1.f;
        float dyaw = Deg2Rad(WrapDeg(f.eye_angles[1] - psi0_deg));
        dyaw_rate[t] = t > 0 ? WrapDeg(f.eye_angles[1] - hist[t - 1].eye_angles[1]) / dt_f : 0.f;
        float pitch = WrapDeg(f.eye_angles[0]);
        float* row = x_seq + t * kSeqDim;
        row[0] = vf[t] / 520.f;
        row[1] = vr[t] / 520.f;
        row[2] = vz[t] / 800.f;
        row[3] = sp2d[t] / maxsp[t];
        row[4] = dvf / 100.f;
        row[5] = dvr / 100.f;
        row[6] = dvz / 100.f;
        row[7] = std::sin(dyaw);
        row[8] = std::cos(dyaw);
        row[9] = std::clamp(dyaw_rate[t] / 15.f, -1.f, 1.f);
        row[10] = std::clamp(pitch / 90.f, -1.f, 1.f);
        row[11] = wf[t];
        row[12] = ws[t];
        row[13] = on_g[t];
        row[14] = (f.flags & 2u) ? 1.f : 0.f;
        row[15] = (f.water_level >= 2 || (f.flags & 1024u)) ? 1.f : 0.f;
        row[16] = HasCond(f.player_cond, 81) ? 1.f : 0.f;
        row[17] = HasCond(f.player_cond, 17) ? 1.f : 0.f;
        row[18] = float(f.air_dash) / 2.f;
        row[19] = f.maxspeed / 520.f;
        row[20] = std::clamp(dt_f / 4.f, 0.25f, 2.f);
        row[21] = float(f.wall_contact);
    }

    std::memset(x_ctx, 0, kCtxDim * sizeof(float));
    if (anchor.class_id >= 1 && anchor.class_id <= 9) x_ctx[anchor.class_id - 1] = 1.f;
    x_ctx[9] = std::clamp(float(anchor.health) / std::max(float(anchor.max_health), 100.f), 0.f, 1.5f);
    x_ctx[10] = (HasCond(anchor.player_cond, 80) || HasCond(anchor.player_cond, 122)) ? 1.f : 0.f;
    x_ctx[11] = float(anchor.air_ducked) / 2.f;
    x_ctx[12] = anchor.jumping > 0 ? 1.f : 0.f;
    x_ctx[13] = sp2d[kSeqLen - 1] / 520.f;
    x_ctx[14] = std::min(std::max(CountRev(ws, kSeqLen, 0.2f), CountRev(vr, kSeqLen, 40.f)), 6.f) / 6.f;
    float yas = 0.f, wfs = 0.f, wss = 0.f;
    for (int t = kSeqLen - 8; t < kSeqLen; ++t) { yas += std::fabs(dyaw_rate[t]); wfs += wf[t]; wss += ws[t]; }
    x_ctx[15] = (yas / 8.f) / 15.f;
    int gr = 0, ar = 0;
    for (int t = kSeqLen - 1; t >= 0 && on_g[t] > 0.5f; --t) ++gr;
    for (int t = kSeqLen - 1; t >= 0 && on_g[t] < 0.5f; --t) ++ar;
    x_ctx[16] = float(gr) / 32.f;
    x_ctx[17] = float(ar) / 32.f;
    x_ctx[18] = wfs / 8.f;
    x_ctx[19] = wss / 8.f;
    x_ctx[20] = anchor.ground_dist / 512.f;
    x_ctx[21] = anchor.ground_normal[0] * c0 + anchor.ground_normal[1] * s0;
    x_ctx[22] = anchor.ground_normal[0] * s0 - anchor.ground_normal[1] * c0;
    x_ctx[23] = anchor.ground_normal[2];
    x_ctx[24] = anchor.ceiling_dist / 512.f;
    float minw = 256.f;
    for (int k = 0; k < 8; ++k) {
        minw = std::min(minw, anchor.wall_dists[k]);
        x_ctx[25 + k] = std::clamp(anchor.wall_dists[k] / 256.f, 0.f, 1.f);
        x_ctx[33 + k] = std::clamp(anchor.ledge_depths[k] / 128.f, -0.5f, 4.f);
    }
    x_ctx[41] = minw / 256.f;
    x_ctx[42] = (anchor.wall_dists[2] + anchor.wall_dists[6]) / 512.f;
    x_ctx[43] = (anchor.ground_normal[2] >= 0.7f && anchor.ground_dist <= 4.f) ? 1.f : 0.f;
}

struct Sample {
    std::array<float, kSeqLen * kSeqDim> x_seq{};
    std::array<float, kCtxDim> x_ctx{};
    std::array<float, kOutSteps * kOutDims> target{};
    uint16_t mask = 0;
    uint32_t steam_id32 = 0;
    int32_t anchor_tick = 0;
    float weight = 1.f;
};

#pragma pack(push, 1)
struct SnrbHeader { char magic[4]; uint32_t version, count, reserved; };
struct SnrbRec {
    float x_seq[kSeqLen * kSeqDim];
    float x_ctx[kCtxDim];
    float target[kOutSteps * kOutDims];
    uint16_t mask, pad;
    uint32_t steam_id32;
    int32_t anchor_tick;
    float weight;
};
#pragma pack(pop)

std::vector<Sample> BuildSamplesFromFrames(const std::vector<ExtractedFrameRecord>& frames, size_t max_samples) {
    std::vector<Sample> out;
    if (frames.size() < kSeqLen + 130) return out;
    for (size_t i = kSeqLen - 1; i + 125 < frames.size() && out.size() < max_samples; i += 8) {
        bool ok = true;
        for (size_t j = i - (kSeqLen - 1); j + 1 <= i + 122; ++j) {
            int dt = frames[j + 1].server_tick - frames[j].server_tick;
            float dx = frames[j + 1].origin[0] - frames[j].origin[0];
            float dy = frames[j + 1].origin[1] - frames[j].origin[1];
            float dz = frames[j + 1].origin[2] - frames[j].origin[2];
            if (frames[j + 1].entity_id != frames[j].entity_id || dt <= 0 || dt > 8 || std::sqrt(dx*dx+dy*dy+dz*dz) > 60.f * float(dt)) {
                ok = false; break;
            }
        }
        if (!ok) continue;

        std::vector<ExtractedFrameRecord> hist(frames.begin() + (i - (kSeqLen - 1)), frames.begin() + i + 1);
        std::vector<std::pair<float, float>> wish(kSeqLen);
        for (int t = 0; t + 1 < kSeqLen; ++t) InferWishDir(hist[t], hist[t + 1], wish[t].first, wish[t].second);
        InferWishDir(hist[kSeqLen - 1], frames[i + 1], wish[kSeqLen - 1].first, wish[kSeqLen - 1].second);

        Sample s;
        s.steam_id32 = static_cast<uint32_t>(frames[i].steam_id & 0xFFFFFFFFull);
        s.anchor_tick = frames[i].server_tick;
        PackWindowFeatures(hist, wish, s.x_seq.data(), s.x_ctx.data());

        int t0 = frames[i].server_tick;
        for (int k = 0; k < 16; ++k) {
            int tgt_tick = t0 + kKnots[k];
            size_t hi = i + 1;
            while (hi < frames.size() && frames[hi].server_tick < tgt_tick) ++hi;
            if (hi + 1 >= frames.size()) continue;
            int dt_hi = frames[hi].server_tick - tgt_tick;
            int dt_lo = tgt_tick - frames[hi - 1].server_tick;
            if (dt_hi > 4 || dt_lo > 4) continue;

            float wf = 0.f, ws = 0.f;
            InferWishDir(frames[hi], frames[hi + 1], wf, ws);
            float dtf = float(std::max(frames[hi].server_tick - frames[hi - 1].server_tick, 1));
            float tr = std::clamp(WrapDeg(frames[hi].eye_angles[1] - frames[hi - 1].eye_angles[1]) / dtf, -15.f, 15.f);

            float* pt = s.target.data() + k * 8;
            pt[0] = wf;
            pt[1] = ws;
            pt[2] = std::clamp(tr / 15.f, -1.f, 1.f);
            pt[3] = frames[hi].jumping > 0 ? 1.f : 0.f;
            pt[4] = (frames[hi].flags & 2u) ? 1.f : 0.f;
            pt[5] = 16.f;
            pt[6] = 8.f;
            pt[7] = 1.f;
            s.mask |= uint16_t(1u << k);
        }
        if (s.mask == 0xFFFFu) out.push_back(std::move(s));
    }
    return out;
}

float EvalControlMse(NeuralCompute::ComputeDispatcher& rt, const std::vector<Sample>& samples) {
    if (samples.empty()) return 0.f;
    uint32_t b = static_cast<uint32_t>(samples.size());
    std::vector<float> xs(b * kSeqLen * kSeqDim), xc(b * kCtxDim), pred(b * kOutSteps * kOutDims);
    for (uint32_t i = 0; i < b; ++i) {
        std::memcpy(xs.data() + size_t(i) * kSeqLen * kSeqDim, samples[i].x_seq.data(), kSeqLen * kSeqDim * 4);
        std::memcpy(xc.data() + size_t(i) * kCtxDim, samples[i].x_ctx.data(), kCtxDim * 4);
    }
    rt.forward(xs.data(), xc.data(), pred.data(), b);
    double sum = 0.0;
    size_t cnt = 0;
    for (uint32_t i = 0; i < b; ++i) {
        for (int k = 0; k < kOutSteps; ++k) {
            for (int c = 0; c < 5; ++c) {
                size_t idx = (size_t(i) * kOutSteps + k) * kOutDims + c;
                double d = double(pred[idx]) - double(samples[i].target[k * 8 + c]);
                sum += d * d;
                ++cnt;
            }
        }
    }
    return float(sum / double(cnt));
}

}

int main() {
    const std::string bin_path = "ml_pipeline/data/extracted/demostf_1291155.bin";
    std::ifstream f(bin_path, std::ios::binary);
    if (!f) {
        std::printf("FAILED: cannot open %s\n", bin_path.c_str());
        return 1;
    }

    std::vector<ExtractedFrameRecord> all(200000);
    f.read(reinterpret_cast<char*>(all.data()), static_cast<std::streamsize>(all.size() * sizeof(ExtractedFrameRecord)));
    size_t n_read = static_cast<size_t>(f.gcount()) / sizeof(ExtractedFrameRecord);
    all.resize(n_read);

    std::vector<uint16_t> eids;
    std::vector<std::vector<ExtractedFrameRecord>> by_eid(256);
    for (const auto& r : all) {
        if (r.entity_id < 256) by_eid[r.entity_id].push_back(r);
    }
    int anchor_eid = -1, target_eid = -1;
    for (int e = 1; e < 256; ++e) {
        if (by_eid[e].size() >= 1500) {
            if (anchor_eid < 0) anchor_eid = e;
            else if (target_eid < 0) { target_eid = e; break; }
        }
    }
    if (anchor_eid < 0 || target_eid < 0) {
        std::printf("FAILED: insufficient player segments\n");
        return 1;
    }

    auto anchor_samples = BuildSamplesFromFrames(by_eid[anchor_eid], 64);
    auto target_all = BuildSamplesFromFrames(by_eid[target_eid], 96);
    if (anchor_samples.size() < 32 || target_all.size() < 64) {
        std::printf("FAILED: insufficient samples (anchor=%zu, target=%zu)\n", anchor_samples.size(), target_all.size());
        return 1;
    }

    std::vector<Sample> target_train(target_all.begin(), target_all.begin() + 32);
    std::vector<Sample> target_holdout(target_all.begin() + 32, target_all.end());

    NeuralCompute::StrafeNetWeights w;
    if (!w.load_snv1_memory(NeuralCompute::g_aEmbeddedWeights, NeuralCompute::g_uEmbeddedWeightsSize)) {
        std::printf("FAILED: load embedded weights\n");
        return 1;
    }

    NeuralCompute::ComputeDispatcher rt;
    rt.init(NeuralCompute::BackendKind::Auto, 64);
    rt.load_weights(w, 32);

    float anchor_mse_before = EvalControlMse(rt, anchor_samples);
    float target_mse_before = EvalControlMse(rt, target_holdout);

    std::vector<Sample> vAnchor = anchor_samples;
    std::deque<Sample> qRecent(target_train.begin(), target_train.end());
    std::mt19937 rng(42u);

    constexpr uint32_t kBatch = 32;
    int promoted = 0;
    for (int step = 0; step < 8; ++step) {
        std::vector<Sample> batch;
        batch.reserve(kBatch);
        std::uniform_int_distribution<size_t> dA(0, vAnchor.size() - 1);
        std::uniform_int_distribution<size_t> dR(0, qRecent.size() - 1);
        for (int i = 0; i < 8; ++i) batch.push_back(vAnchor[dA(rng)]);
        for (int i = 0; i < 24; ++i) batch.push_back(qRecent[dR(rng)]);

        std::vector<float> xs(kBatch * kSeqLen * kSeqDim), xc(kBatch * kCtxDim), tgt(kBatch * kOutSteps * kOutDims), pred(kBatch * kOutSteps * kOutDims);
        for (uint32_t b = 0; b < kBatch; ++b) {
            std::memcpy(xs.data() + size_t(b) * kSeqLen * kSeqDim, batch[b].x_seq.data(), kSeqLen * kSeqDim * 4);
            std::memcpy(xc.data() + size_t(b) * kCtxDim, batch[b].x_ctx.data(), kCtxDim * 4);
            std::memcpy(tgt.data() + size_t(b) * kOutSteps * kOutDims, batch[b].target.data(), kOutSteps * kOutDims * 4);
        }
        rt.forward(xs.data(), xc.data(), pred.data(), kBatch);
        for (uint32_t b = 0; b < kBatch; ++b) {
            for (int k = 0; k < kOutSteps; ++k) {
                for (int c = 5; c <= 6; ++c) {
                    size_t idx = (size_t(b) * kOutSteps + k) * kOutDims + c;
                    tgt[idx] = std::max(1.f, pred[idx] + std::clamp(tgt[idx] - pred[idx], -0.5f, 0.5f));
                }
            }
        }
        rt.train_cfg.lr = 3e-4f;
        rt.train_cfg.lambda_anchor = 1e-2f;
        rt.train_cfg.max_grad_norm = 1.f;
        auto m = rt.train_step(xs.data(), xc.data(), tgt.data(), kBatch);
        if (m.applied) ++promoted;
    }

    float anchor_mse_after = EvalControlMse(rt, anchor_samples);
    float target_mse_after = EvalControlMse(rt, target_holdout);

    const std::string tmp_w = "ml_pipeline/cpp_bench/test_neural_strafe.bin";
    const std::string tmp_r = "ml_pipeline/cpp_bench/test_neural_replay.bin";
    bool saved_w = rt.weights.save_snv1(tmp_w);
    bool saved_r = false;
    {
        std::ofstream rf(tmp_r, std::ios::binary | std::ios::trunc);
        SnrbHeader h{ {'S','N','R','B'}, 1, static_cast<uint32_t>(vAnchor.size() + qRecent.size()), 0 };
        rf.write(reinterpret_cast<const char*>(&h), sizeof(h));
        auto wr = [&](const Sample& s) {
            SnrbRec rec{};
            std::memcpy(rec.x_seq, s.x_seq.data(), sizeof(rec.x_seq));
            std::memcpy(rec.x_ctx, s.x_ctx.data(), sizeof(rec.x_ctx));
            std::memcpy(rec.target, s.target.data(), sizeof(rec.target));
            rec.mask = s.mask;
            rec.steam_id32 = s.steam_id32;
            rec.anchor_tick = s.anchor_tick;
            rec.weight = s.weight;
            rf.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
        };
        for (const auto& s : vAnchor) wr(s);
        for (const auto& s : qRecent) wr(s);
        saved_r = rf.good();
    }

    NeuralCompute::StrafeNetWeights w_reload;
    bool loaded_w = w_reload.load_snv1(tmp_w);
    float max_diff = loaded_w ? NeuralCompute::strafenet::max_abs_diff(rt.weights.plastic_theta.data(), w_reload.plastic_theta.data(), NeuralCompute::ModelDims::PLASTIC_TOTAL) : 999.f;

    SnrbHeader h_reload{};
    {
        std::ifstream rf(tmp_r, std::ios::binary);
        rf.read(reinterpret_cast<char*>(&h_reload), sizeof(h_reload));
    }

    std::error_code ec;
    std::filesystem::remove(tmp_w, ec);
    std::filesystem::remove(tmp_r, ec);
    rt.destroy();

    std::printf("promoted=%d\n", promoted);
    std::printf("target_ctrl_mse: before=%.5f after=%.5f\n", target_mse_before, target_mse_after);
    std::printf("anchor_ctrl_mse: before=%.5f after=%.5f\n", anchor_mse_before, anchor_mse_after);
    std::printf("save_load_weights: saved=%d loaded=%d max_diff=%.8f\n", saved_w, loaded_w, max_diff);
    std::printf("save_load_replay: saved=%d magic=%.4s count=%u\n", saved_r, h_reload.magic, h_reload.count);

    bool ok = promoted == 8
        && target_mse_after < target_mse_before
        && anchor_mse_after <= anchor_mse_before * 1.05f
        && saved_w && loaded_w && max_diff == 0.f
        && saved_r && std::memcmp(h_reload.magic, "SNRB", 4) == 0 && h_reload.count == vAnchor.size() + qRecent.size();

    std::printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
