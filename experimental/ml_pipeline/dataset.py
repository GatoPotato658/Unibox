import json
import math
from pathlib import Path
from typing import Any

import numpy as np
import torch
from torch.utils.data import Dataset

T_HIST = 32
HORIZON_KNOTS = np.array([1, 2, 4, 6, 9, 12, 16, 21, 27, 34, 42, 52, 64, 78, 96, 120], dtype=np.int32)
MAX_HORIZON = 120
UNSEEN_TEST_FAMILIES = {"koth_bagel", "cp_metalworks"}
AIR_HEAVY_FAMILIES = {"koth_ultiduo", "ultiduo_baloo", "ctf_ballin", "ctf_bball", "mge_training"}
PINNED_TEST_IDS = {
    "demostf_1403967",
    "demostf_1484116",
    "demostf_1535981",
    "demostf_1536408",
    "demostf_1536372",
    "demostf_1536376",
    "demostf_1523840",
    "demostf_1536267",
    "demostf_1291155",
    "demostf_1311530",
    "demostf_1440360",
    "demostf_1445341",
}
PINNED_VAL_IDS = {
    "demostf_1536373",
    "demostf_1536378",
    "etf2l_92868_pov",
    "demostf_1484115",
    "etf2l_92888_pov",
    "demostf_1324269",
    "demostf_1446689",
    "etf2l_93069_pov",
}

FRAME_DTYPE = np.dtype([
    ("steam_id", "<u8"),
    ("demo_tick", "<i4"),
    ("server_tick", "<i4"),
    ("origin", "<f4", 3),
    ("velocity", "<f4", 3),
    ("eye_angles", "<f4", 2),
    ("wish_fwd", "<f4"),
    ("wish_side", "<f4"),
    ("maxspeed", "<f4"),
    ("sim_time", "<f4"),
    ("ground_dist", "<f4"),
    ("ground_normal", "<f4", 3),
    ("ceiling_dist", "<f4"),
    ("wall_dists", "<f4", 8),
    ("ledge_depths", "<f4", 8),
    ("player_cond", "<u4", 5),
    ("flags", "<u4"),
    ("buttons", "<u4"),
    ("active_weapon", "<i4"),
    ("entity_id", "<u2"),
    ("health", "<i2"),
    ("max_health", "<i2"),
    ("team", "u1"),
    ("class_id", "u1"),
    ("water_level", "u1"),
    ("air_dash", "u1"),
    ("air_ducked", "u1"),
    ("jumping", "u1"),
    ("vel_source", "u1"),
    ("input_source", "u1"),
    ("wall_contact", "u1"),
    ("pad", "u1", 5),
])

assert FRAME_DTYPE.itemsize == 200


def build_strict_split(manifest_path: Path, summary_path: Path) -> dict[str, list[dict[str, Any]]]:
    mf = json.loads(manifest_path.read_text(encoding="utf-8"))
    bs = json.loads(summary_path.read_text(encoding="utf-8"))
    meta_by_demo = {Path(d["demo_path"]).name: d for d in bs["demos"]}

    demos = []
    for d in mf["demos"]:
        name = Path(d["local_path"]).name
        m = meta_by_demo.get(name, {})
        min_frames = 10000 if d.get("map_family") in AIR_HEAVY_FAMILIES else 50000
        if (
            not m
            or m.get("map", "").lower() != d.get("map", "").lower()
            or Path(d.get("bsp_path", "")).stem.lower() != m.get("map", "").lower()
            or (d["id"] not in PINNED_TEST_IDS and d["id"] not in PINNED_VAL_IDS and m.get("total_frames", 0) < min_frames)
        ):
            continue
        rec = dict(d)
        rec["bin_path"] = str(summary_path.parent / f"{d['id']}.bin")
        rec["meta"] = m
        demos.append(rec)

    missing_test = PINNED_TEST_IDS - {d["id"] for d in demos}
    if missing_test:
        raise ValueError(f"Missing pinned test demos: {sorted(missing_test)}")

    def overlaps(a: dict[str, Any], b: dict[str, Any]) -> bool:
        if a.get("sha256") and a["sha256"] == b.get("sha256"):
            return True
        ta, tb = int(a.get("time") or 0), int(b.get("time") or 0)
        return bool(ta and tb and a["map_family"] == b["map_family"] and abs(ta - tb) < 3 * 3600)

    train_demos: list[dict[str, Any]] = []
    val_demos: list[dict[str, Any]] = []
    test_demos = [d for d in demos if d["id"] in PINNED_TEST_IDS]
    remaining_by_fam: dict[str, list[dict[str, Any]]] = {}

    for d in demos:
        did = d["id"]
        if did in PINNED_TEST_IDS or any(overlaps(d, t) for t in test_demos):
            continue
        if d["map_family"] in UNSEEN_TEST_FAMILIES:
            continue
        elif did in PINNED_VAL_IDS:
            val_demos.append(d)
        else:
            remaining_by_fam.setdefault(d["map_family"], []).append(d)

    val_fams = {d["map_family"] for d in val_demos}
    for fam, items in sorted(remaining_by_fam.items()):
        items_sorted = sorted(items, key=lambda x: (not x["is_pov"], x["id"]))
        if fam not in val_fams and len(items_sorted) >= 2:
            val_demos.append(items_sorted[0])
            val_fams.add(fam)
            train_demos.extend(items_sorted[1:])
        else:
            train_demos.extend(items_sorted)

    train_demos = [d for d in train_demos if not any(overlaps(d, v) for v in val_demos)]
    return {"train": train_demos, "val": val_demos, "test": test_demos}


def wrap_deg(a: np.ndarray | float) -> np.ndarray | float:
    return (a + 180.0) % 360.0 - 180.0


def has_cond(cond: np.ndarray, bit: int) -> np.ndarray:
    w = bit // 32
    b = bit % 32
    return ((cond[..., w] >> b) & 1) != 0


def count_reversals(arr: np.ndarray, thresh: float = 0.2) -> float:
    vals = arr[np.abs(arr) > thresh]
    if len(vals) < 2:
        return 0.0
    signs = np.sign(vals)
    return float(np.sum(signs[1:] != signs[:-1]))


def compute_features_and_targets(
    hist: np.ndarray,
    fut_ticks: np.ndarray,
    fut_frames: np.ndarray,
) -> dict[str, Any] | None:
    n_h = len(hist)
    if n_h != T_HIST:
        return None
    dt_h = np.diff(hist["server_tick"])
    if np.any(dt_h <= 0) or np.any(dt_h > 8):
        return None
    dpos_h = np.linalg.norm(np.diff(hist["origin"], axis=0), axis=1)
    if np.any(dpos_h > 60.0 * dt_h):
        return None

    anchor = hist[-1]
    t0 = int(anchor["server_tick"])
    psi0_deg = float(anchor["eye_angles"][1])
    psi0 = math.radians(psi0_deg)
    c0 = math.cos(psi0)
    s0 = math.sin(psi0)

    def to_ego_xy(vx: np.ndarray, vy: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        return vx * c0 + vy * s0, vx * s0 - vy * c0

    vx_h = hist["velocity"][:, 0]
    vy_h = hist["velocity"][:, 1]
    vz_h = hist["velocity"][:, 2]
    vf_h, vr_h = to_ego_xy(vx_h, vy_h)
    sp2d_h = np.hypot(vx_h, vy_h)
    maxsp_h = np.maximum(hist["maxspeed"], 100.0)

    dvf_h = np.zeros(T_HIST, dtype=np.float32)
    dvr_h = np.zeros(T_HIST, dtype=np.float32)
    dvz_h = np.zeros(T_HIST, dtype=np.float32)
    dvf_h[1:] = vf_h[1:] - vf_h[:-1]
    dvr_h[1:] = vr_h[1:] - vr_h[:-1]
    dvz_h[1:] = vz_h[1:] - vz_h[:-1]

    yaws_h = hist["eye_angles"][:, 1]
    dyaw_rad = np.radians(wrap_deg(yaws_h - psi0_deg))
    dt_full = np.ones(T_HIST, dtype=np.float32)
    dt_full[1:] = dt_h.astype(np.float32)
    dyaw_rate = np.zeros(T_HIST, dtype=np.float32)
    dyaw_rate[1:] = wrap_deg(yaws_h[1:] - yaws_h[:-1]) / dt_full[1:]
    pitch_h = wrap_deg(hist["eye_angles"][:, 0])

    flags_h = hist["flags"]
    on_g_h = ((flags_h & 1) != 0).astype(np.float32)
    duck_h = ((flags_h & 2) != 0).astype(np.float32)
    in_wat_h = ((hist["water_level"] >= 2) | ((flags_h & 1024) != 0)).astype(np.float32)
    blast_h = has_cond(hist["player_cond"], 81).astype(np.float32)
    charge_h = has_cond(hist["player_cond"], 17).astype(np.float32)

    x_seq = np.zeros((T_HIST, 22), dtype=np.float32)
    x_seq[:, 0] = vf_h / 520.0
    x_seq[:, 1] = vr_h / 520.0
    x_seq[:, 2] = vz_h / 800.0
    x_seq[:, 3] = sp2d_h / maxsp_h
    x_seq[:, 4] = dvf_h / 100.0
    x_seq[:, 5] = dvr_h / 100.0
    x_seq[:, 6] = dvz_h / 100.0
    x_seq[:, 7] = np.sin(dyaw_rad)
    x_seq[:, 8] = np.cos(dyaw_rad)
    x_seq[:, 9] = np.clip(dyaw_rate / 15.0, -1.0, 1.0)
    x_seq[:, 10] = np.clip(pitch_h / 90.0, -1.0, 1.0)
    x_seq[:, 11] = np.clip(hist["wish_fwd"], -1.0, 1.0)
    x_seq[:, 12] = np.clip(hist["wish_side"], -1.0, 1.0)
    x_seq[:, 13] = on_g_h
    x_seq[:, 14] = duck_h
    x_seq[:, 15] = in_wat_h
    x_seq[:, 16] = blast_h
    x_seq[:, 17] = charge_h
    x_seq[:, 18] = hist["air_dash"].astype(np.float32) / 2.0
    x_seq[:, 19] = hist["maxspeed"] / 520.0
    x_seq[:, 20] = np.clip(dt_full / 4.0, 0.25, 2.0)
    x_seq[:, 21] = hist["wall_contact"].astype(np.float32)

    x_ctx = np.zeros(44, dtype=np.float32)
    cls_id = int(anchor["class_id"])
    if 1 <= cls_id <= 9:
        x_ctx[cls_id - 1] = 1.0
    hp_ratio = float(anchor["health"]) / max(float(anchor["max_health"]), 100.0)
    x_ctx[9] = min(max(hp_ratio, 0.0), 1.5)
    para = bool(has_cond(anchor["player_cond"], 80) or has_cond(anchor["player_cond"], 122))
    x_ctx[10] = 1.0 if para else 0.0
    x_ctx[11] = float(anchor["air_ducked"]) / 2.0
    x_ctx[12] = 1.0 if anchor["jumping"] > 0 else 0.0
    x_ctx[13] = float(sp2d_h[-1]) / 520.0

    rev_side = count_reversals(hist["wish_side"], 0.2)
    rev_vel = count_reversals(vr_h, 40.0)
    x_ctx[14] = min(max(rev_side, rev_vel), 6.0) / 6.0
    x_ctx[15] = float(np.mean(np.abs(dyaw_rate[-8:]))) / 15.0

    g_run = 0
    for v in on_g_h[::-1]:
        if v > 0.5:
            g_run += 1
        else:
            break
    a_run = 0
    for v in on_g_h[::-1]:
        if v < 0.5:
            a_run += 1
        else:
            break
    x_ctx[16] = g_run / 32.0
    x_ctx[17] = a_run / 32.0
    x_ctx[18] = float(np.mean(x_seq[-8:, 11]))
    x_ctx[19] = float(np.mean(x_seq[-8:, 12]))

    gn = anchor["ground_normal"]
    gn_f, gn_r = to_ego_xy(np.array([gn[0]]), np.array([gn[1]]))
    x_ctx[20] = float(anchor["ground_dist"]) / 512.0
    x_ctx[21] = float(gn_f[0])
    x_ctx[22] = float(gn_r[0])
    x_ctx[23] = float(gn[2])
    x_ctx[24] = float(anchor["ceiling_dist"]) / 512.0
    wd = anchor["wall_dists"]
    ld = anchor["ledge_depths"]
    x_ctx[25:33] = np.clip(wd / 256.0, 0.0, 1.0)
    x_ctx[33:41] = np.clip(ld / 128.0, -0.5, 4.0)
    x_ctx[41] = float(np.min(wd)) / 256.0
    x_ctx[42] = float(wd[2] + wd[6]) / 512.0
    x_ctx[43] = 1.0 if (gn[2] >= 0.7 and anchor["ground_dist"] <= 4.0) else 0.0

    target_ticks = t0 + HORIZON_KNOTS
    idx_hi = np.searchsorted(fut_ticks, target_ticks, side="left")
    if np.any(idx_hi >= len(fut_ticks)):
        return None
    idx_lo = np.maximum(idx_hi - 1, 0)
    t_lo = fut_ticks[idx_lo]
    t_hi = fut_ticks[idx_hi]
    if np.any((t_hi - target_ticks) > 4) or np.any((target_ticks - t_lo) > 4):
        return None

    denom = np.maximum(t_hi - t_lo, 1).astype(np.float32)
    alpha = np.where(t_hi == t_lo, 0.0, (target_ticks - t_lo) / denom).astype(np.float32)

    o_lo = fut_frames["origin"][idx_lo]
    o_hi = fut_frames["origin"][idx_hi]
    org_k = o_lo + (o_hi - o_lo) * alpha[:, None]
    dorg = org_k - anchor["origin"][None, :]
    pf, pr = to_ego_xy(dorg[:, 0], dorg[:, 1])
    y_pos = np.stack([pf, pr, dorg[:, 2]], axis=1).astype(np.float32)

    v_lo = fut_frames["velocity"][idx_lo]
    v_hi = fut_frames["velocity"][idx_hi]
    vel_k = v_lo + (v_hi - v_lo) * alpha[:, None]
    vf_k, vr_k = to_ego_xy(vel_k[:, 0], vel_k[:, 1])
    y_vel = np.stack([vf_k, vr_k, vel_k[:, 2]], axis=1).astype(np.float32)

    wf_k = fut_frames["wish_fwd"][idx_hi].astype(np.float32)
    ws_k = fut_frames["wish_side"][idx_hi].astype(np.float32)
    f_prev_idx = np.maximum(idx_hi - 1, 0)
    dt_fut = np.maximum(fut_ticks[idx_hi] - fut_ticks[f_prev_idx], 1).astype(np.float32)
    yaw_fut_prev = np.where(idx_hi > 0, fut_frames["eye_angles"][f_prev_idx, 1], psi0_deg)
    tr_k = np.clip(wrap_deg(fut_frames["eye_angles"][idx_hi, 1] - yaw_fut_prev) / dt_fut, -15.0, 15.0).astype(np.float32)
    p_jump_k = ((fut_frames["jumping"][idx_hi] > 0) | ((fut_frames["buttons"][idx_hi] & 2) != 0)).astype(np.float32)
    p_duck_k = ((fut_frames["flags"][idx_hi] & 2) != 0).astype(np.float32)
    y_ctrl = np.stack([wf_k, ws_k, tr_k, p_jump_k, p_duck_k], axis=1).astype(np.float32)

    init_state = np.array([
        float(vf_h[-1]),
        float(vr_h[-1]),
        float(vz_h[-1]),
        float(anchor["maxspeed"]),
        float(on_g_h[-1]),
        float(duck_h[-1]),
        float(in_wat_h[-1]),
        float(charge_h[-1]),
        float(anchor["ground_dist"]),
    ], dtype=np.float32)

    fut_h_mask = (fut_ticks > t0) & (fut_ticks <= t0 + MAX_HORIZON)
    if np.any(fut_h_mask):
        t_sub = fut_ticks[fut_h_mask]
        dts = np.clip(np.diff(t_sub, prepend=t0), 1, 8)
        f_air = (fut_frames["flags"][fut_h_mask] & 1) == 0
        fut_air_ticks = int(np.sum(dts[f_air]))
    else:
        fut_air_ticks = 0
    is_air_traj = 1 if (on_g_h[-1] < 0.5 or blast_h[-1] > 0.5 or fut_air_ticks >= 12) else 0

    return {
        "x_seq": x_seq,
        "x_ctx": x_ctx,
        "y_pos": y_pos,
        "y_vel": y_vel,
        "y_ctrl": y_ctrl,
        "init_state": init_state,
        "input_source": int(anchor["input_source"]),
        "fut_air_ticks": min(fut_air_ticks, 255),
        "is_air_traj": is_air_traj,
    }


def extract_windows_from_demo(
    bin_path: Path,
    max_windows: int,
    choke_aug_prob: float = 0.0,
    rng: np.random.Generator | None = None,
    keep_raw_hist: bool = False,
    air_oversample: bool = False,
) -> dict[str, np.ndarray]:
    if rng is None:
        rng = np.random.default_rng(42)
    arr = np.memmap(bin_path, dtype=FRAME_DTYPE, mode="r")
    n = len(arr)
    if n < T_HIST + MAX_HORIZON + 10:
        return {}

    eids = arr["entity_id"]
    ticks = arr["server_tick"]
    pos = arr["origin"]
    not_on_g = (arr["flags"] & 1) == 0
    blast_arr = has_cond(arr["player_cond"], 81)
    in_src_arr = arr["input_source"]

    eid_change = np.empty(n, dtype=bool)
    eid_change[0] = True
    eid_change[1:] = eids[1:] != eids[:-1]

    dt = np.ones(n, dtype=np.int32)
    dt[1:] = ticks[1:] - ticks[:-1]
    dpos = np.zeros(n, dtype=np.float32)
    dpos[1:] = np.linalg.norm(pos[1:] - pos[:-1], axis=1)

    break_mask = eid_change | (dt <= 0) | (dt > 8) | (dpos > 60.0 * np.maximum(dt, 1))
    seg_ids = np.cumsum(break_mask)

    _, seg_starts, seg_counts = np.unique(seg_ids, return_index=True, return_counts=True)

    candidates: list[tuple[int, int, int, int, int]] = []
    for s_start, s_len in zip(seg_starts, seg_counts):
        if s_len < T_HIST + MAX_HORIZON + 2:
            continue
        s_end = s_start + s_len
        seg_ticks = ticks[s_start:s_end]
        max_anchor_tick = int(seg_ticks[-1]) - MAX_HORIZON
        if max_anchor_tick <= int(seg_ticks[T_HIST - 1]):
            continue
        valid_end = int(np.searchsorted(seg_ticks, max_anchor_tick, side="right"))
        seg_air_cs = np.cumsum(not_on_g[s_start:s_end], dtype=np.int32)
        stride = 8
        start_off = T_HIST * 3 if choke_aug_prob > 0 else T_HIST - 1
        for local_idx in range(start_off, valid_end, stride):
            if local_idx < T_HIST - 1:
                continue
            abs_idx = int(s_start + local_idx)
            look_hi = min(int(s_len) - 1, local_idx + MAX_HORIZON)
            approx_air = int(seg_air_cs[look_hi] - seg_air_cs[local_idx])
            is_air_c = 1 if (not_on_g[abs_idx] or blast_arr[abs_idx] or approx_air >= 10) else 0
            candidates.append((int(s_start), int(s_end), abs_idx, int(in_src_arr[abs_idx]), is_air_c))

    if not candidates:
        return {}

    if len(candidates) > max_windows:
        if air_oversample:
            pov_air = [c for c in candidates if c[3] == 1 and c[4] == 1]
            stv_air = [c for c in candidates if c[3] == 0 and c[4] == 1]
            pov_gnd = [c for c in candidates if c[3] == 1 and c[4] == 0]
            stv_gnd = [c for c in candidates if c[3] == 0 and c[4] == 0]
            chosen = []
            air_budget = (max_windows * 55) // 100
            take_pa = min(len(pov_air), air_budget)
            if take_pa > 0:
                chosen.extend(pov_air[i] for i in rng.choice(len(pov_air), size=take_pa, replace=False))
            rem_air = air_budget - len(chosen)
            take_sa = min(len(stv_air), rem_air)
            if take_sa > 0:
                chosen.extend(stv_air[i] for i in rng.choice(len(stv_air), size=take_sa, replace=False))
            rem_total = max_windows - len(chosen)
            take_pg = min(len(pov_gnd), max(rem_total // 2, 0))
            if take_pg > 0:
                chosen.extend(pov_gnd[i] for i in rng.choice(len(pov_gnd), size=take_pg, replace=False))
            rem_total = max_windows - len(chosen)
            take_sg = min(len(stv_gnd), rem_total)
            if take_sg > 0:
                chosen.extend(stv_gnd[i] for i in rng.choice(len(stv_gnd), size=take_sg, replace=False))
            if len(chosen) < max_windows:
                chosen_set = {c[2] for c in chosen}
                leftover = [c for c in candidates if c[2] not in chosen_set]
                if leftover:
                    extra = min(len(leftover), max_windows - len(chosen))
                    chosen.extend(leftover[i] for i in rng.choice(len(leftover), size=extra, replace=False))
        else:
            pov_cands = [c for c in candidates if c[3] == 1]
            stv_cands = [c for c in candidates if c[3] == 0]
            if pov_cands:
                n_pov = min(len(pov_cands), max_windows // 2)
                n_stv = max_windows - n_pov
                sel_pov = rng.choice(len(pov_cands), size=n_pov, replace=False)
                chosen = [pov_cands[i] for i in sel_pov]
                if stv_cands and n_stv > 0:
                    sel_stv = rng.choice(len(stv_cands), size=min(len(stv_cands), n_stv), replace=False)
                    chosen.extend(stv_cands[i] for i in sel_stv)
            else:
                sel = rng.choice(len(candidates), size=max_windows, replace=False)
                chosen = [candidates[i] for i in sel]
    else:
        chosen = candidates

    xs, xc, yp, yv, yc, ist, isrc, fair, iair = [], [], [], [], [], [], [], [], []
    raw_h_list = []

    for s_start, s_end, abs_idx, _, _ in chosen:
        hist_step = 1
        if choke_aug_prob > 0.0 and rng.random() < choke_aug_prob:
            hist_step = int(rng.choice([2, 3]))
        h_start = abs_idx - (T_HIST - 1) * hist_step
        if h_start < s_start:
            hist_step = 1
            h_start = abs_idx - (T_HIST - 1)
            if h_start < s_start:
                continue
        hist = arr[h_start : abs_idx + 1 : hist_step]
        fut_end = min(s_end, abs_idx + MAX_HORIZON + 16)
        fut_frames = arr[abs_idx:fut_end]
        fut_ticks = ticks[abs_idx:fut_end]

        res = compute_features_and_targets(hist, fut_ticks, fut_frames)
        if res is None:
            continue
        xs.append(res["x_seq"])
        xc.append(res["x_ctx"])
        yp.append(res["y_pos"])
        yv.append(res["y_vel"])
        yc.append(res["y_ctrl"])
        ist.append(res["init_state"])
        isrc.append(res["input_source"])
        fair.append(res["fut_air_ticks"])
        iair.append(res["is_air_traj"])
        if keep_raw_hist:
            rh = np.zeros((T_HIST, 11), dtype=np.float32)
            rh[:, 0:3] = hist["origin"]
            rh[:, 3:6] = hist["velocity"]
            rh[:, 6] = hist["eye_angles"][:, 1]
            rh[:, 7] = hist["maxspeed"]
            rh[:, 8] = hist["flags"].astype(np.float32)
            rh[:, 9] = hist["server_tick"].astype(np.float32)
            rh[:, 10] = hist["wall_contact"].astype(np.float32)
            raw_h_list.append(rh)

    if not xs:
        return {}
    out = {
        "x_seq": np.stack(xs, axis=0),
        "x_ctx": np.stack(xc, axis=0),
        "y_pos": np.stack(yp, axis=0),
        "y_vel": np.stack(yv, axis=0),
        "y_ctrl": np.stack(yc, axis=0),
        "init_state": np.stack(ist, axis=0),
        "input_source": np.array(isrc, dtype=np.uint8),
        "fut_air_ticks": np.array(fair, dtype=np.uint8),
        "is_air_traj": np.array(iair, dtype=np.uint8),
    }
    if keep_raw_hist:
        out["raw_hist"] = np.stack(raw_h_list, axis=0)
    return out


def build_or_load_cache(
    cache_dir: Path = Path("ml_pipeline/data/cache"),
    manifest_path: Path = Path("ml_pipeline/data/manifest.json"),
    summary_path: Path = Path("ml_pipeline/data/extracted/batch_summary.json"),
    train_target: int = 850000,
    val_target: int = 80000,
    test_target: int = 60000,
    force_rebuild: bool = False,
    memory_mapped: bool = False,
) -> dict[str, Any]:
    cache_dir.mkdir(parents=True, exist_ok=True)
    split_file = cache_dir / "split_info.json"
    split_paths = [cache_dir / s if memory_mapped else cache_dir / f"{s}.npz" for s in ("train", "val")]
    if not force_rebuild and split_file.exists() and all(p.exists() for p in split_paths + [cache_dir / "test.npz"]):
        return json.loads(split_file.read_text(encoding="utf-8"))

    splits = build_strict_split(manifest_path, summary_path)
    rng = np.random.default_rng(2026)
    split_stats: dict[str, Any] = {}
    old_split: dict[str, Any] = {}
    if split_file.exists():
        try:
            old_split = json.loads(split_file.read_text(encoding="utf-8"))
        except Exception:
            old_split = {}

    targets = {"train": train_target, "val": val_target, "test": test_target}
    for sname in ("train", "val", "test"):
        if (
            sname == "test"
            and (cache_dir / "test.npz").exists()
            and set(old_split.get("test", {}).get("demo_ids", [])) == PINNED_TEST_IDS
        ):
            split_stats["test"] = old_split["test"]
            print(
                f"Preserved test: {split_stats['test']['demos']} demos, "
                f"{split_stats['test']['windows']:,} windows (pinned apples-to-apples)",
                flush=True,
            )
            continue

        demos = splits[sname]
        per_demo = max(1200, targets[sname] // max(len(demos), 1))
        choke_p = 0.15 if sname == "train" else 0.0
        keep_raw = sname == "test"
        air_os = sname in ("train", "val")
        chunks: list[dict[str, np.ndarray]] = []
        mapped: dict[str, np.ndarray] = {}
        count = 0
        used_demos = []
        raw_frames_sum = 0
        for i, d in enumerate(demos, 1):
            bp = Path(d["bin_path"])
            if not bp.exists():
                continue
            raw_frames_sum += int(d["meta"].get("total_frames", 0))
            chunk = extract_windows_from_demo(
                bp,
                per_demo,
                choke_aug_prob=choke_p,
                rng=rng,
                keep_raw_hist=keep_raw,
                air_oversample=air_os,
            )
            if chunk:
                used_demos.append(d)
                n_w = len(chunk["x_seq"])
                chunk["is_unseen_map"] = np.full(
                    n_w, 1 if d["map_family"] in UNSEEN_TEST_FAMILIES else 0, dtype=np.uint8
                )
                if memory_mapped and sname != "test":
                    if not mapped:
                        mapped_dir = cache_dir / sname
                        mapped_dir.mkdir()
                        capacity = per_demo * len(demos)
                        mapped = {
                            k: np.lib.format.open_memmap(
                                mapped_dir / f"{k}.npy", mode="w+", dtype=v.dtype, shape=(capacity, *v.shape[1:])
                            )
                            for k, v in chunk.items()
                        }
                    for k, v in chunk.items():
                        mapped[k][count:count + n_w] = v
                    count += n_w
                else:
                    chunks.append(chunk)
            if i % 25 == 0 or i == len(demos):
                print(f"Preparing {sname}: {i}/{len(demos)} demos", flush=True)

        if mapped:
            for v in mapped.values():
                v.flush()
            merged = {k: v[:count] for k, v in mapped.items()}
        elif chunks:
            merged = {k: np.concatenate([c[k] for c in chunks], axis=0) for k in chunks[0]}
            if sname == "train":
                perm = rng.permutation(len(merged["x_seq"]))
                for k in merged:
                    merged[k] = merged[k][perm]
            np.savez(cache_dir / f"{sname}.npz", **merged)
        else:
            raise ValueError(f"No usable {sname} windows")
        split_stats[sname] = {
            "demos": len(used_demos),
            "demo_ids": [d["id"] for d in used_demos],
            "map_families": sorted({d["map_family"] for d in used_demos}),
            "raw_frames": raw_frames_sum,
            "windows": int(len(merged["x_seq"])),
            "pov_windows": int((merged["input_source"] == 1).sum()),
            "stv_windows": int((merged["input_source"] == 0).sum()),
            "air_traj_windows": int((merged["is_air_traj"] == 1).sum()),
        }
        print(
            f"Cached {sname}: {split_stats[sname]['demos']} demos, {split_stats[sname]['raw_frames']:,} raw frames, "
            f"{split_stats[sname]['windows']:,} windows (POV={split_stats[sname]['pov_windows']:,}, air={split_stats[sname]['air_traj_windows']:,})",
            flush=True,
        )

    split_file.write_text(json.dumps(split_stats, indent=2), encoding="utf-8")
    return split_stats


class TrajectoryDataset(Dataset):
    def __init__(self, npz_path: Path):
        data = np.load(npz_path, mmap_mode="r")
        self.x_seq = np.array(data["x_seq"], dtype=np.float32)
        self.x_ctx = np.array(data["x_ctx"], dtype=np.float32)
        self.y_pos = np.array(data["y_pos"], dtype=np.float32)
        self.y_vel = np.array(data["y_vel"], dtype=np.float32)
        self.y_ctrl = np.array(data["y_ctrl"], dtype=np.float32)
        self.init_state = np.array(data["init_state"], dtype=np.float32)
        self.input_source = np.array(data["input_source"], dtype=np.float32)

    def __len__(self) -> int:
        return len(self.x_seq)

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, ...]:
        return (
            torch.from_numpy(self.x_seq[idx]),
            torch.from_numpy(self.x_ctx[idx]),
            torch.from_numpy(self.y_pos[idx]),
            torch.from_numpy(self.y_vel[idx]),
            torch.from_numpy(self.y_ctrl[idx]),
            torch.from_numpy(self.init_state[idx]),
            torch.tensor(self.input_source[idx], dtype=torch.float32),
        )


if __name__ == "__main__":
    stats = build_or_load_cache(force_rebuild=True)
    print(json.dumps(stats, indent=2))
