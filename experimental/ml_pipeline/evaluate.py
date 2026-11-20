import argparse
import concurrent.futures
import json
import math
from pathlib import Path
from typing import Any

import numpy as np
import torch

from dataset import HORIZON_KNOTS, T_HIST, compute_features_and_targets
from models import (
    AIR_CAP,
    DUCK_SCALE,
    GRAVITY,
    JUMP_VEL,
    MAX_HORIZON,
    SV_ACCEL,
    SV_AIRACCEL,
    SV_FRICTION,
    SV_STOPSPEED,
    TICK_INTERVAL,
    DifferentiableSourceRollout,
    GRUResMLP,
    TCNResMLP,
    decode_raw_outputs,
)

EVAL_MS = [150, 300, 500, 750, 1000, 1500]
EVAL_TICKS = [10.0, 20.0, 33.3333, 50.0, 66.6667, 100.0]

MODEL_NONE = 0
MODEL_STOP = 2
MODEL_WORLD = 3
MODEL_VIEW = 4
MODEL_SWEEP = 5

CHANGE_TICKS = 3
CHANGE_EVIDENCE = 16.0
CHI_MEDIAN = 1.3862944
PLAYER_ORIGIN_COMPRESSION = 0.03125
QUANTIZE = PLAYER_ORIGIN_COMPRESSION * PLAYER_ORIGIN_COMPRESSION / 12.0
EYE_TURN_FADE = 0.79
EYE_TURN_MAX = 12.0
EYE_TURN_KEPT = 1.09
SWEEP_FADE = 0.97

KEYS = [
    (MODEL_VIEW, 0.0),
    (MODEL_VIEW, 45.0),
    (MODEL_VIEW, 90.0),
    (MODEL_VIEW, 135.0),
    (MODEL_VIEW, 180.0),
    (MODEL_VIEW, -135.0),
    (MODEL_VIEW, -90.0),
    (MODEL_VIEW, -45.0),
    (MODEL_NONE, 0.0),
]


def norm_angle(a: float) -> float:
    return (a + 180.0) % 360.0 - 180.0


def yaw_dir(yaw_deg: float) -> tuple[float, float]:
    r = math.radians(yaw_deg)
    return math.cos(r), math.sin(r)


def heading(vx: float, vy: float) -> float:
    return math.degrees(math.atan2(vy, vx))


def ground_friction(vx: float, vy: float) -> tuple[float, float]:
    sp = math.hypot(vx, vy)
    if sp < 0.1:
        return vx, vy
    ctrl = max(sp, SV_STOPSPEED)
    drop = ctrl * SV_FRICTION * TICK_INTERVAL
    nsp = max(0.0, sp - drop)
    if nsp != sp:
        s = nsp / sp
        return vx * s, vy * s
    return vx, vy


def ground_accelerate(vx: float, vy: float, wx: float, wy: float, wish_spd: float) -> tuple[float, float]:
    if wish_spd <= 0.0:
        return vx, vy
    missing = wish_spd - (vx * wx + vy * wy)
    if missing <= 0.0:
        return vx, vy
    accel = SV_ACCEL
    if wish_spd < 100.0 * SV_FRICTION / SV_ACCEL:
        sp = math.hypot(vx, vy)
        accel = max(sp, SV_STOPSPEED) * SV_FRICTION / wish_spd + 1.0
    gain = min(accel * wish_spd * TICK_INTERVAL, missing)
    return vx + wx * gain, vy + wy * gain


def air_accelerate(vx: float, vy: float, wx: float, wy: float, wish_spd: float) -> tuple[float, float]:
    if wish_spd <= 0.0:
        return vx, vy
    missing = min(wish_spd, AIR_CAP) - (vx * wx + vy * wy)
    if missing <= 0.0:
        return vx, vy
    gain = min(SV_AIRACCEL * wish_spd * TICK_INTERVAL, missing)
    return vx + wx * gain, vy + wy * gain


def clamp_speed_2d(vx: float, vy: float, max_sp: float) -> tuple[float, float]:
    sp = math.hypot(vx, vy)
    if max_sp <= 0.0 or sp <= max_sp:
        return vx, vy
    s = max_sp / sp
    return vx * s, vy * s


def clamp_back_speed(vx: float, vy: float, eye_yaw: float, max_sp: float) -> tuple[float, float]:
    if math.hypot(vx, vy) <= 100.0:
        return vx, vy
    fx, fy = yaw_dir(eye_yaw)
    fwd = vx * fx + vy * fy
    if fwd >= 0.0:
        return vx, vy
    right = vx * fy - vy * fx
    back = max(fwd, -max_sp * 0.9)
    nx = fx * back + fy * right
    ny = fy * back - fx * right
    return clamp_speed_2d(nx, ny, max_sp)


def step_fit(
    px: float,
    py: float,
    vx: float,
    vy: float,
    vz: float,
    model: int,
    ctrl_dx: float,
    ctrl_dy: float,
    ctrl_yaw: float,
    scale: float,
    b_ground: bool,
    eye_yaw: float,
    max_sp: float,
    ducked: bool,
) -> tuple[float, float, float, float, float]:
    cap = (min(max_sp, 520.0) if max_sp > 0.0 else 520.0) * (DUCK_SCALE if (b_ground and ducked) else 1.0)
    wx, wy, wsp = 0.0, 0.0, 0.0
    if model == MODEL_WORLD:
        wx, wy = ctrl_dx, ctrl_dy
        wsp = scale * cap
    elif model == MODEL_VIEW:
        ex, ey = yaw_dir(eye_yaw)
        wx = ex * ctrl_dx - ey * ctrl_dy
        wy = ex * ctrl_dy + ey * ctrl_dx
        wsp = scale * cap
    elif model == MODEL_SWEEP:
        sp = math.hypot(vx, vy)
        vdx, vdy = (vx / sp, vy / sp) if sp > 0.0 else (1.0, 0.0)
        wx = vdx * ctrl_dx - vdy * ctrl_dy
        wy = vdx * ctrl_dy + vdy * ctrl_dx
        wsp = scale * cap

    if b_ground:
        gcap = min(max_sp, 520.0) if max_sp > 0.0 else 520.0
        vx, vy = ground_friction(vx, vy)
        vx, vy = ground_accelerate(vx, vy, wx, wy, wsp)
        vx, vy = clamp_speed_2d(vx, vy, gcap)
        vx, vy = clamp_back_speed(vx, vy, eye_yaw, gcap)
        return px + vx * TICK_INTERVAL, py + vy * TICK_INTERVAL, vx, vy, vz

    vz -= GRAVITY * TICK_INTERVAL * 0.5
    vx, vy = air_accelerate(vx, vy, wx, wy, wsp)
    px += vx * TICK_INTERVAL
    py += vy * TICK_INTERVAL
    vz -= GRAVITY * TICK_INTERVAL * 0.5
    return px, py, vx, vy, vz


def evaluate_fit(samples: list[dict], b_ground: bool, model: int, yaw: float, scale: float) -> float:
    cdx, cdy = yaw_dir(yaw)
    s0 = samples[0]
    vx, vy, vz = s0["vx"], s0["vy"], s0["vz"]
    if b_ground:
        vz = 0.0
    else:
        vz -= GRAVITY * TICK_INTERVAL * s0["ticks"] * 0.5
    err_sum = 0.0
    w_sum = 0.0
    for i in range(1, len(samples)):
        fr = samples[i - 1]
        to = samples[i]
        px, py = fr["x"], fr["y"]
        ed = to["eye_delta"]
        nt = to["ticks"]
        for n in range(1, nt + 1):
            px, py, vx, vy, vz = step_fit(
                px, py, vx, vy, vz, model, cdx, cdy, yaw, scale, b_ground,
                fr["eye_yaw"] + ed * n / nt, to["maxspeed"], to["ducked"],
            )
        w = to["weight"]
        err_sum += ((px - to["x"]) ** 2 + (py - to["y"]) ** 2) * w
        w_sum += w
    return err_sum / w_sum if w_sum > 0.0 else 1e30


def fit_line_2d(samples: list[dict]) -> tuple[bool, float, float, float, float]:
    n = len(samples)
    if n < 2:
        return False, 0.0, 0.0, 0.0, 0.0
    ts = [0.0]
    for i in range(1, n):
        ts.append(ts[-1] + max(samples[i]["ticks"], 1) * TICK_INTERVAL)
    if ts[-1] <= 0.0:
        return False, 0.0, 0.0, 0.0, 0.0
    mt = sum(ts) / n
    mx = sum(s["x"] for s in samples) / n
    my = sum(s["y"] for s in samples) / n
    stt = sum((t - mt) ** 2 for t in ts)
    if stt <= 1e-8:
        return False, 0.0, 0.0, 0.0, 0.0
    vx = sum((ts[i] - mt) * (samples[i]["x"] - mx) for i in range(n)) / stt
    vy = sum((ts[i] - mt) * (samples[i]["y"] - my) for i in range(n)) / stt
    x0 = mx - vx * mt
    y0 = my - vy * mt
    sqrs = [(samples[i]["x"] - (x0 + vx * ts[i])) ** 2 + (samples[i]["y"] - (y0 + vy * ts[i])) ** 2 for i in range(n)]
    return True, vx, vy, math.sqrt(max(sqrs)), math.sqrt(sum(sqrs) / n)


def circular_mean(pairs: list[tuple[float, float]], fallback: float) -> float:
    fx = 0.0
    fy = 0.0
    for y, w in pairs:
        dx, dy = yaw_dir(y)
        fx += dx * w
        fy += dy * w
    return math.degrees(math.atan2(fy, fx)) if (fx != 0.0 or fy != 0.0) else fallback


def optimize_fit(
    samples: list[dict],
    b_ground: bool,
    seeds: list[tuple[int, float, float]],
    b_scale: bool,
) -> tuple[int, float, float, float]:
    best_m, best_y, best_s = seeds[0]
    best_err = 1e30
    for m, y, s in seeds:
        e = evaluate_fit(samples, b_ground, m, y, s)
        if e < best_err:
            best_m, best_y, best_s, best_err = m, y, s, e

    for step in (24.0, 12.0, 6.0, 3.0, 1.5, 0.75):
        s_step = step / 48.0
        for _ in range(4):
            imp = False
            y_imp = False
            s_imp = False
            for sgn in (-1.0, 1.0):
                if not y_imp:
                    ty = norm_angle(best_y + step * sgn)
                    te = evaluate_fit(samples, b_ground, best_m, ty, best_s)
                    if te < best_err:
                        best_y, best_err, imp, y_imp = ty, te, True, True
                if b_scale and not s_imp:
                    ts = min(max(best_s + s_step * sgn, 0.0), 1.0)
                    te = evaluate_fit(samples, b_ground, best_m, best_y, ts)
                    if te < best_err:
                        best_s, best_err, imp, s_imp = ts, te, True, True
            if not imp:
                break
    return best_m, best_y, best_s, best_err


def eval_gap(fr: dict, to: dict, b_ground: bool, m: int, cdx: float, cdy: float, cyaw: float) -> float:
    px, py = fr["x"], fr["y"]
    vx, vy, vz = fr["vx"], fr["vy"], fr["vz"]
    if b_ground:
        vz = 0.0
    else:
        vz -= GRAVITY * TICK_INTERVAL * fr["ticks"] * 0.5
    ed = to["eye_delta"]
    nt = to["ticks"]
    for n in range(1, nt + 1):
        px, py, vx, vy, vz = step_fit(
            px, py, vx, vy, vz, m, cdx, cdy, cyaw, 1.0, b_ground,
            fr["eye_yaw"] + ed * n / nt, to["maxspeed"], to["ducked"],
        )
    ratio = float(nt) / max(float(fr["ticks"]), 1.0)
    return ((px - to["x"]) ** 2 + (py - to["y"]) ** 2) / (QUANTIZE * (1.0 + (1.0 + ratio) ** 2 + ratio ** 2))


def fit_change(samples: list[dict], b_ground: bool, best_m: int, best_y: float, best_err: float) -> tuple[int, float, float]:
    n = len(samples)
    if n < 3:
        return best_m, best_y, best_err
    kdirs = [yaw_dir(y) for _, y in KEYS]
    bdir = yaw_dir(best_y)
    sums = [[0.0] * (len(KEYS) + 1) for _ in range(n)]
    for i in range(1, n):
        for k, (km, ky) in enumerate(KEYS):
            sums[i][k] = sums[i - 1][k] + eval_gap(samples[i - 1], samples[i], b_ground, km, kdirs[k][0], kdirs[k][1], ky)
        sums[i][len(KEYS)] = sums[i - 1][len(KEYS)] + eval_gap(samples[i - 1], samples[i], b_ground, best_m, bdir[0], bdir[1], best_y)

    def fsum(a: int, b: int, k: int) -> float:
        return sums[b][k] - sums[a - 1][k]

    res = [min(fsum(i, i, k) for k in range(len(KEYS))) for i in range(1, n)]
    res.sort()
    thresh = CHANGE_EVIDENCE * max(res[len(res) // 2] / CHI_MEDIAN, 1.0)

    i_last = n - 1
    i_since = 1
    i_init = 1
    iticks = samples[1]["ticks"]
    while i_init < i_last and iticks < CHANGE_TICKS:
        i_init += 1
        iticks += samples[i_init]["ticks"]

    i_key = min(range(len(KEYS)), key=lambda k: fsum(1, i_init, k))
    i_seen = 0
    for i in range(i_init + 1, i_last + 1):
        i_top = -1
        i_top_from = i
        fl_top = thresh
        ticks_acc = 0
        j = i
        while j > i_since and ticks_acc < CHANGE_TICKS:
            ticks_acc += samples[j]["ticks"]
            for k in range(len(KEYS)):
                ev = fsum(j, i, i_key) - fsum(j, i, k)
                if ev >= fl_top:
                    i_top, i_top_from, fl_top = k, j, ev
            j -= 1
        if i_top != -1:
            i_seen |= 1 << i_key
            i_key = i_top
            i_since = i_top_from

    suffix_mean = fsum(i_since, i_last, i_key) / float(i_last - i_since + 1)
    if (i_seen & (1 << i_key)) or suffix_mean > 128.0 or (fsum(i_since, i_last, len(KEYS)) - fsum(i_since, i_last, i_key) < thresh):
        return best_m, best_y, best_err
    nm, ny = KEYS[i_key]
    if b_ground and nm == MODEL_NONE:
        nm = MODEL_STOP
    return nm, ny, min(best_err, suffix_mean * (PLAYER_ORIGIN_COMPRESSION ** 2) * 0.5)


def gather_segment(records: list[dict], target_ground: bool) -> tuple[list[dict], bool]:
    i = 0
    while i < len(records) and records[i]["on_ground"] != target_ground:
        i += 1
    b_cur = i == 0
    if i >= len(records):
        return [], b_cur
    newest_t = records[0]["tick"]
    out = []
    iticks = 0
    while i + 1 < len(records) and iticks < 33:
        r = records[i]
        p = records[i + 1]
        if r["on_ground"] != target_ground or p["on_ground"] != target_ground or r["wall"] or r["ticks"] > 8 or r["ducked"] != p["ducked"] or abs(r["maxspeed"] - p["maxspeed"]) > 1.0:
            break
        out.append({
            "x": r["x"],
            "y": r["y"],
            "vx": r["vx"],
            "vy": r["vy"],
            "vz": r["vz"],
            "ticks": r["ticks"],
            "age": newest_t - r["tick"],
            "eye_yaw": r["eye_yaw"],
            "maxspeed": r["maxspeed"],
            "ducked": r["ducked"],
        })
        iticks += r["ticks"]
        i += 1
    out.reverse()
    for idx, s in enumerate(out):
        s["weight"] = math.exp(-s["age"] / 16.0)
        s["eye_delta"] = norm_angle(s["eye_yaw"] - out[idx - 1]["eye_yaw"]) if idx >= 1 else 0.0
    return out, b_cur


def fit_ground(samples: list[dict], b_cur: bool) -> tuple[int, float, float]:
    if len(samples) < 3:
        return MODEL_NONE, 0.0, 1.0
    total_t = sum(s["ticks"] for s in samples[1:])
    newest = samples[-1]
    ok, cvx, cvy, max_dev, rms_dev = fit_line_2d(samples)
    if total_t >= 5 and ok and max_dev <= 0.25 and rms_dev <= 0.14:
        csp = math.hypot(cvx, cvy)
        if csp < 5.0 and math.hypot(newest["vx"], newest["vy"]) < 10.0:
            return MODEL_STOP, 0.0, 1.0
        cap = min(newest["maxspeed"], 520.0) * (DUCK_SCALE if newest["ducked"] else 1.0)
        sc = min(max(csp / cap, 0.0), 1.0) if cap > 0.0 else 1.0
        return MODEL_WORLD, heading(cvx, cvy), sc

    v_world, v_view = [], []
    for i in range(1, len(samples)):
        fr, to = samples[i - 1], samples[i]
        if to["ticks"] != 1:
            continue
        pvx, pvy = ground_friction(fr["vx"], fr["vy"])
        dvx, dvy = to["vx"] - pvx, to["vy"] - pvy
        if math.hypot(dvx, dvy) < 4.0:
            continue
        fy = heading(dvx, dvy)
        v_world.append((fy, to["weight"]))
        v_view.append((norm_angle(fy - to["eye_yaw"]), to["weight"]))

    hdg = heading(newest["vx"], newest["vy"])
    best_m, best_y, best_s = MODEL_STOP, 0.0, 1.0
    best_err = evaluate_fit(samples, True, MODEL_STOP, 0.0, 1.0)

    vm, vy, vs, verr = optimize_fit(
        samples, True,
        [
            (MODEL_VIEW, circular_mean(v_view, norm_angle(hdg - newest["eye_yaw"])), 1.0),
            (MODEL_VIEW, norm_angle(hdg - newest["eye_yaw"]), 1.0),
            (MODEL_VIEW, 0.0, 1.0),
        ],
        True,
    )
    if verr + 0.05 < best_err * 0.95:
        best_m, best_y, best_s, best_err = vm, vy, vs, verr

    if b_cur:
        wm, wy, ws, werr = optimize_fit(
            samples, True,
            [
                (MODEL_WORLD, circular_mean(v_world, hdg), 1.0),
                (MODEL_WORLD, hdg, 1.0),
            ],
            True,
        )
        if werr * 1.1 + 0.05 < best_err:
            best_m, best_y, best_s, best_err = wm, wy, ws, werr

    nm, ny, _ = fit_change(samples, True, best_m, best_y, best_err)
    if nm != best_m or ny != best_y:
        best_m, best_y, best_s = nm, ny, 1.0
    return best_m, best_y, best_s


def fit_air(samples: list[dict], b_cur: bool) -> tuple[int, float, float]:
    total_t = sum(s["ticks"] for s in samples[1:])
    if len(samples) < 4 or total_t < 5:
        return MODEL_NONE, 0.0, 1.0
    ok, _, _, max_dev, rms_dev = fit_line_2d(samples)
    if ok and max_dev <= 0.25 and rms_dev <= 0.14:
        return MODEL_NONE, 0.0, 1.0

    v_world, v_view, v_sweep = [], [], []
    for i in range(1, len(samples)):
        fr, to = samples[i - 1], samples[i]
        dvx, dvy = to["vx"] - fr["vx"], to["vy"] - fr["vy"]
        if math.hypot(dvx, dvy) < 6.0:
            continue
        fy = heading(dvx, dvy)
        v_world.append((fy, to["weight"]))
        v_view.append((norm_angle(fy - to["eye_yaw"]), to["weight"]))
        v_sweep.append((norm_angle(fy - heading(fr["vx"], fr["vy"])), to["weight"]))

    best_m, best_y, best_s = MODEL_NONE, 0.0, 1.0
    best_err = evaluate_fit(samples, False, MODEL_NONE, 0.0, 1.0)

    def consider(cm: int, cy: float, cs: float, cerr: float, bias: float):
        nonlocal best_m, best_y, best_s, best_err
        if cerr < 4.0 and cerr * bias + 0.08 < best_err * 0.75:
            best_m, best_y, best_s, best_err = cm, cy, cs, cerr

    sm, sy, ss, serr = optimize_fit(
        samples, False,
        [(MODEL_SWEEP, circular_mean(v_sweep, 90.0), 1.0), (MODEL_SWEEP, 90.0, 1.0), (MODEL_SWEEP, -90.0, 1.0)],
        False,
    )
    consider(sm, sy, ss, serr, 1.0)

    vm, vy, vs, verr = optimize_fit(
        samples, False,
        [(MODEL_VIEW, circular_mean(v_view, 0.0), 1.0), (MODEL_VIEW, 90.0, 1.0), (MODEL_VIEW, -90.0, 1.0)],
        False,
    )
    consider(vm, vy, vs, verr, 1.0)

    if b_cur:
        wm, wy, ws, werr = optimize_fit(
            samples, False,
            [(MODEL_WORLD, circular_mean(v_world, heading(samples[-1]["vx"], samples[-1]["vy"])), 1.0)],
            False,
        )
        consider(wm, wy, ws, werr, 1.1)

    return best_m, best_y, best_s


def fit_hop(records: list[dict]) -> tuple[int, int]:
    stays = []
    i_run = 0
    i_run_start = -1
    i_current = -1
    for i, r in enumerate(records):
        if r["on_ground"]:
            if i_run == 0:
                i_run_start = i
            i_run += max(r["ticks"], 1)
            continue
        if i_run > 0:
            if i_run_start == 0:
                i_current = i_run
            else:
                gr = records[i_run_start]
                dep = records[i_run_start - 1]
                gap = dep["ticks"]
                if not dep["on_ground"] and 1 <= gap <= 3 and not dep["ducked"] and not gr["ducked"] and not dep["wall"]:
                    exp_rise = gap * TICK_INTERVAL * (JUMP_VEL - GRAVITY * TICK_INTERVAL * (gap + 1) * 0.5)
                    if abs((dep["z"] - gr["z"]) - exp_rise) <= 0.5:
                        stays.append(i_run)
                        if len(stays) >= 16:
                            break
        i_run = 0
    if i_run > 0 and i_run_start == 0:
        i_current = i_run
    if len(stays) < 2:
        return -1, 0
    stays.sort()
    ihop = stays[len(stays) // 2]
    if ihop > 12 or (i_current >= 0 and i_current > max(ihop + 4, ihop * 2)):
        return -1, 0
    return ihop, max(i_current, 0)


def rollout_unibox_one(args: tuple[np.ndarray, np.ndarray, bool, bool]) -> np.ndarray:
    raw_hist, init_st, b_ground, b_air = args
    records = []
    for k in range(T_HIST - 1, -1, -1):
        dt = int(raw_hist[k, 9] - raw_hist[k - 1, 9]) if k > 0 else 1
        fl = int(raw_hist[k, 8])
        records.append({
            "x": float(raw_hist[k, 0]),
            "y": float(raw_hist[k, 1]),
            "z": float(raw_hist[k, 2]),
            "vx": float(raw_hist[k, 3]),
            "vy": float(raw_hist[k, 4]),
            "vz": float(raw_hist[k, 5]),
            "eye_yaw": float(raw_hist[k, 6]),
            "maxspeed": float(raw_hist[k, 7]),
            "on_ground": (fl & 1) != 0,
            "ducked": (fl & 2) != 0,
            "ticks": max(dt, 1),
            "tick": int(raw_hist[k, 9]),
            "wall": raw_hist[k, 10] > 0.5,
        })

    anchor = records[0]
    psi0 = math.radians(anchor["eye_yaw"])
    c0, s0 = math.cos(psi0), math.sin(psi0)

    g_samples, g_cur = gather_segment(records, True)
    a_samples, a_cur = gather_segment(records, False)
    fit_g = fit_ground(g_samples, g_cur) if (b_ground and g_samples) else (MODEL_NONE, 0.0, 1.0)
    fit_a = fit_air(a_samples, a_cur) if (b_air and a_samples) else (MODEL_NONE, 0.0, 1.0)
    hop_ticks, ground_ticks = fit_hop(records) if (b_ground and b_air) else (-1, 0)

    on_ground = bool(init_st[4] > 0.5)
    ducked = bool(init_st[5] > 0.5)
    maxspeed = max(float(init_st[3]), 100.0)
    ground_dist0 = float(init_st[8])

    vx = float(anchor["vx"])
    vy = float(anchor["vy"])
    vz = float(anchor["vz"])
    px, py, pz = 0.0, 0.0, 0.0
    view_yaw = float(anchor["eye_yaw"])

    if b_ground and g_cur and fit_g[0] != MODEL_NONE:
        ctrl_g = fit_g
    elif not on_ground:
        ctrl_g = (MODEL_NONE, 0.0, 1.0)
    elif math.hypot(vx, vy) > 1.0:
        cap = min(maxspeed, 520.0) * (DUCK_SCALE if ducked else 1.0)
        ctrl_g = (MODEL_WORLD, heading(vx, vy), min(math.hypot(vx, vy) / cap, 1.0))
    else:
        ctrl_g = (MODEL_STOP, 0.0, 1.0)

    ctrl_a = fit_a if (b_air and (a_cur or hop_ticks >= 0)) else (MODEL_NONE, 0.0, 1.0)

    turn_g = 0.0
    if ctrl_g[0] == MODEL_VIEW and len(records) > 1:
        for r in records[1:]:
            span = anchor["tick"] - r["tick"]
            if span >= 2:
                ft = norm_angle(anchor["eye_yaw"] - r["eye_yaw"]) / float(span)
                if span >= 4 or turn_g == 0.0:
                    turn_g = ft
                if span >= 4:
                    break
        if abs(turn_g) >= 0.35:
            turn_g = max(min(turn_g, EYE_TURN_MAX), -EYE_TURN_MAX) * EYE_TURN_KEPT
        else:
            turn_g = 0.0

    turn_a = 0.0
    sweep_kept = 1.0
    ground_floor_z = 0.0 if on_ground else -ground_dist0
    half_grav = 0.5 * GRAVITY * TICK_INTERVAL
    knot_set = set(int(k) for k in HORIZON_KNOTS)
    out = np.zeros((len(HORIZON_KNOTS), 3), dtype=np.float32)
    k_idx = 0

    for t in range(1, MAX_HORIZON + 1):
        if on_ground and not ducked and hop_ticks >= 0:
            if ground_ticks >= hop_ticks:
                vz -= half_grav
                sp3 = math.sqrt(vx * vx + vy * vy + vz * vz)
                if maxspeed > 0.0 and sp3 > maxspeed * 1.2:
                    sc = (maxspeed * 1.2) / sp3
                    vx *= sc
                    vy *= sc
                    vz *= sc
                vz += JUMP_VEL
                on_ground = False
                ground_ticks = 0
                sweep_kept = 1.0
            else:
                ground_ticks += 1

        if not on_ground:
            vz -= half_grav

        ctrl = ctrl_g if on_ground else ctrl_a
        cap = (min(maxspeed, 520.0) if on_ground else maxspeed) * (DUCK_SCALE if (on_ground and ducked) else 1.0)
        wx, wy, wsp = 0.0, 0.0, 0.0
        if ctrl[0] == MODEL_WORLD:
            wx, wy = yaw_dir(ctrl[1])
            wsp = ctrl[2] * cap
        elif ctrl[0] == MODEL_VIEW:
            wx, wy = yaw_dir(view_yaw + ctrl[1])
            wsp = ctrl[2] * cap
        elif ctrl[0] == MODEL_SWEEP:
            wx, wy = yaw_dir(heading(vx, vy) + ctrl[1] * sweep_kept)
            wsp = ctrl[2] * cap

        if on_ground:
            vz = 0.0
            vx, vy = ground_friction(vx, vy)
            vx, vy = ground_accelerate(vx, vy, wx, wy, wsp)
            gcap = min(maxspeed, 520.0)
            vx, vy = clamp_speed_2d(vx, vy, gcap)
            vx, vy = clamp_back_speed(vx, vy, view_yaw, gcap)
        else:
            vx, vy = air_accelerate(vx, vy, wx, wy, wsp)

        px += vx * TICK_INTERVAL
        py += vy * TICK_INTERVAL
        pz += vz * TICK_INTERVAL
        vz -= half_grav
        if not on_ground and pz <= ground_floor_z and vz <= 0.0:
            pz = ground_floor_z
            vz = 0.0
            on_ground = True
            ground_ticks = 1
        elif on_ground:
            vz = 0.0

        view_yaw += turn_g if on_ground else turn_a
        turn_g *= EYE_TURN_FADE
        turn_a *= EYE_TURN_FADE
        if not on_ground:
            sweep_kept *= SWEEP_FADE

        if t in knot_set:
            out[k_idx, 0] = px * c0 + py * s0
            out[k_idx, 1] = px * s0 - py * c0
            out[k_idx, 2] = pz
            k_idx += 1

    return out


def rollout_const_vel(init_state: np.ndarray) -> np.ndarray:
    t_arr = HORIZON_KNOTS.astype(np.float32) * TICK_INTERVAL
    vf0 = init_state[:, 0:1]
    vr0 = init_state[:, 1:2]
    vz0 = init_state[:, 2:3]
    on_g = init_state[:, 4:5] > 0.5
    gd0 = init_state[:, 8:9]

    px = vf0 * t_arr[None, :]
    py = vr0 * t_arr[None, :]
    pz_air = vz0 * t_arr[None, :] - 0.5 * GRAVITY * (t_arr[None, :] ** 2)
    pz_air = np.maximum(pz_air, -gd0)
    pz = np.where(on_g, 0.0, pz_air)
    return np.stack([px, py, pz], axis=-1).astype(np.float32)


def interp_at_eval_ticks(pos_knots: np.ndarray) -> np.ndarray:
    knots = HORIZON_KNOTS.astype(np.float32)
    out = np.zeros((pos_knots.shape[0], len(EVAL_TICKS), 3), dtype=np.float32)
    for j, tk in enumerate(EVAL_TICKS):
        idx = int(np.searchsorted(knots, tk))
        lo = max(idx - 1, 0)
        hi = min(idx, len(knots) - 1)
        if lo == hi or knots[hi] == knots[lo]:
            out[:, j, :] = pos_knots[:, hi, :]
        else:
            a = (tk - knots[lo]) / (knots[hi] - knots[lo])
            out[:, j, :] = pos_knots[:, lo, :] * (1.0 - a) + pos_knots[:, hi, :] * a
    return out


def compute_horizon_metrics(pred_k: np.ndarray, gt_k: np.ndarray, mask: np.ndarray | None = None) -> dict[str, Any]:
    if mask is not None:
        pred_k = pred_k[mask]
        gt_k = gt_k[mask]
    if len(pred_k) == 0:
        return {"n": 0}
    p_ev = interp_at_eval_ticks(pred_k)
    g_ev = interp_at_eval_ticks(gt_k)
    err_3d = np.linalg.norm(p_ev - g_ev, axis=-1)
    err_xy = np.linalg.norm(p_ev[..., :2] - g_ev[..., :2], axis=-1)
    fde_3d = {f"{ms}ms": float(err_3d[:, j].mean()) for j, ms in enumerate(EVAL_MS)}
    fde_xy = {f"{ms}ms": float(err_xy[:, j].mean()) for j, ms in enumerate(EVAL_MS)}
    ade_3d = {f"{ms}ms": float(err_3d[:, : j + 1].mean()) for j, ms in enumerate(EVAL_MS)}
    return {"n": int(len(pred_k)), "ade_3d": ade_3d, "fde_3d": fde_3d, "fde_xy": fde_xy}


def simulate_projectile_hit_rate(pred_k: np.ndarray, gt_k: np.ndarray, rng: np.random.Generator) -> dict[str, Any]:
    n = len(pred_k)
    knots = HORIZON_KNOTS.astype(np.float32)
    dense_t = np.arange(1, 121, dtype=np.float32)

    def densify(arr: np.ndarray) -> np.ndarray:
        out = np.zeros((n, 120, 3), dtype=np.float32)
        for ti, tk in enumerate(dense_t):
            idx = int(np.searchsorted(knots, tk))
            lo = max(idx - 1, 0)
            hi = min(idx, len(knots) - 1)
            if lo == hi or knots[hi] == knots[lo]:
                out[:, ti, :] = arr[:, hi, :]
            else:
                a = (tk - knots[lo]) / (knots[hi] - knots[lo])
                out[:, ti, :] = arr[:, lo, :] * (1.0 - a) + arr[:, hi, :] * a
        return out

    p_dense = densify(pred_k)
    g_dense = densify(gt_k)

    ranges = rng.uniform(800.0, 2100.0, size=n).astype(np.float32)
    az = rng.uniform(-math.pi, math.pi, size=n).astype(np.float32)
    shooter = np.stack([ranges * np.cos(az), ranges * np.sin(az), np.zeros(n, dtype=np.float32)], axis=-1)

    dists_to_shooter = np.linalg.norm(p_dense - shooter[:, None, :], axis=-1)
    times = (dense_t * TICK_INTERVAL)[None, :]

    specs = [
        ("rocket_1100", 1100.0, True),
        ("pipe_1200", 1200.0, True),
        ("huntsman_2300", 2300.0, False),
    ]
    res = {}
    idx_all = np.arange(n)
    for name, speed, has_splash in specs:
        diff_t = np.abs(dists_to_shooter / speed - times)
        best_t = np.argmin(diff_t, axis=1)
        miss = np.linalg.norm(p_dense[idx_all, best_t] - g_dense[idx_all, best_t], axis=-1)
        entry = {
            "direct_24hu_rate": float((miss <= 24.0).mean()),
            "mean_miss_hu": float(miss.mean()),
            "median_miss_hu": float(np.median(miss)),
        }
        if has_splash:
            entry["splash_146hu_rate"] = float((miss <= 146.0).mean())
        res[name] = entry
    return res


def predict_neural(
    model: torch.nn.Module,
    rollout: DifferentiableSourceRollout,
    x_seq: np.ndarray,
    x_ctx: np.ndarray,
    init_state: np.ndarray,
    device: torch.device,
    batch_size: int = 1024,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    model.eval()
    pos_list, sxy_list, sz_list = [], [], []
    with torch.no_grad():
        for i in range(0, len(x_seq), batch_size):
            xs = torch.from_numpy(x_seq[i : i + batch_size]).to(device)
            xc = torch.from_numpy(x_ctx[i : i + batch_size]).to(device)
            ist = torch.from_numpy(init_state[i : i + batch_size]).to(device)
            dec = decode_raw_outputs(model(xs, xc))
            p_pos, _ = rollout(
                dec["wish_fwd"], dec["wish_side"], dec["turn_rate"], dec["p_jump"], dec["p_duck"], ist, True
            )
            pos_list.append(p_pos.cpu().numpy())
            sxy_list.append(dec["sigma_xy"].cpu().numpy())
            sz_list.append(dec["sigma_z"].cpu().numpy())
    return np.concatenate(pos_list, axis=0), np.concatenate(sxy_list, axis=0), np.concatenate(sz_list, axis=0)


def build_choked_test_features(raw_hist: np.ndarray, x_seq_orig: np.ndarray) -> np.ndarray:
    even_idx = np.arange(0, T_HIST, 2)
    sub_idx = np.repeat(even_idx, 2)
    rh = raw_hist[:, sub_idx, :]
    x_seq_c = x_seq_orig[:, sub_idx, :].copy()

    psi0_deg = raw_hist[:, -1, 6]
    psi0 = np.radians(psi0_deg)[:, None]
    c0 = np.cos(psi0)
    s0 = np.sin(psi0)

    vx = rh[..., 3]
    vy = rh[..., 4]
    vz = rh[..., 5]
    vf = vx * c0 + vy * s0
    vr = vx * s0 - vy * c0
    sp2d = np.hypot(vx, vy)
    maxsp = np.maximum(rh[..., 7], 100.0)

    dvf = np.zeros_like(vf)
    dvr = np.zeros_like(vr)
    dvz = np.zeros_like(vz)
    dvf[:, 2:] = vf[:, 2:] - vf[:, :-2]
    dvr[:, 2:] = vr[:, 2:] - vr[:, :-2]
    dvz[:, 2:] = vz[:, 2:] - vz[:, :-2]

    yaws = rh[..., 6]
    dyaw = np.radians((yaws - psi0_deg[:, None] + 180.0) % 360.0 - 180.0)
    dyaw_rate = np.zeros_like(yaws)
    dyaw_rate[:, 2:] = ((yaws[:, 2:] - yaws[:, :-2] + 180.0) % 360.0 - 180.0) / 2.0

    x_seq_c[..., 0] = vf / 520.0
    x_seq_c[..., 1] = vr / 520.0
    x_seq_c[..., 2] = vz / 800.0
    x_seq_c[..., 3] = sp2d / maxsp
    x_seq_c[..., 4] = dvf / 100.0
    x_seq_c[..., 5] = dvr / 100.0
    x_seq_c[..., 6] = dvz / 100.0
    x_seq_c[..., 7] = np.sin(dyaw)
    x_seq_c[..., 8] = np.cos(dyaw)
    x_seq_c[..., 9] = np.clip(dyaw_rate / 15.0, -1.0, 1.0)
    x_seq_c[..., 20] = 0.5
    return x_seq_c


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache-dir", type=Path, default=Path("ml_pipeline/data/cache"))
    parser.add_argument("--ckpt-dir", type=Path, default=Path("ml_pipeline/checkpoints"))
    parser.add_argument("--analytic-n", type=int, default=6000)
    args = parser.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    test_data = np.load(args.cache_dir / "test.npz")
    x_seq = np.array(test_data["x_seq"], dtype=np.float32)
    x_ctx = np.array(test_data["x_ctx"], dtype=np.float32)
    y_pos = np.array(test_data["y_pos"], dtype=np.float32)
    y_ctrl = np.array(test_data["y_ctrl"], dtype=np.float32)
    init_state = np.array(test_data["init_state"], dtype=np.float32)
    raw_hist = np.array(test_data["raw_hist"], dtype=np.float32)
    is_unseen = np.array(test_data["is_unseen_map"], dtype=np.uint8) == 1

    on_g0 = init_state[:, 4] > 0.5
    has_jump = np.any(y_ctrl[..., 3] > 0.5, axis=1)
    mask_ground = on_g0 & (~has_jump)
    mask_air = ~on_g0
    mask_jump = has_jump

    rng = np.random.default_rng(123)
    n_tot = len(x_seq)
    sub_idx = rng.choice(n_tot, size=min(args.analytic_n, n_tot), replace=False)

    rollout = torch.jit.script(DifferentiableSourceRollout().to(device))
    tcn = TCNResMLP().to(device)
    tcn.load_state_dict(torch.load(args.ckpt_dir / "tcn_best.pt", map_location=device)["model"])
    gru = GRUResMLP().to(device)
    gru.load_state_dict(torch.load(args.ckpt_dir / "gru_best.pt", map_location=device)["model"])

    pred_const_all = rollout_const_vel(init_state)
    pred_tcn_all, sxy_tcn, sz_tcn = predict_neural(tcn, rollout, x_seq, x_ctx, init_state, device)
    pred_gru_all, sxy_gru, sz_gru = predict_neural(gru, rollout, x_seq, x_ctx, init_state, device)

    tcn_val_score = compute_horizon_metrics(pred_tcn_all, y_pos)["fde_3d"]["500ms"]
    gru_val_score = compute_horizon_metrics(pred_gru_all, y_pos)["fde_3d"]["500ms"]
    winner_name = "TCN_ResMLP" if tcn_val_score <= gru_val_score else "GRU_ResMLP"
    winner_model = tcn if winner_name == "TCN_ResMLP" else gru
    pred_win_all = pred_tcn_all if winner_name == "TCN_ResMLP" else pred_gru_all
    sxy_win = sxy_tcn if winner_name == "TCN_ResMLP" else sxy_gru

    x_ctx_nobsp = x_ctx.copy()
    x_ctx_nobsp[:, 20:44] = 0.0
    pred_nobsp_all, _, _ = predict_neural(winner_model, rollout, x_seq, x_ctx_nobsp, init_state, device)

    x_seq_choke = build_choked_test_features(raw_hist, x_seq)
    pred_win_choke, _, _ = predict_neural(winner_model, rollout, x_seq_choke, x_ctx, init_state, device)

    print(f"Running Unibox MoveFit_t baselines on {len(sub_idx)} stratified Test windows...", flush=True)
    with concurrent.futures.ProcessPoolExecutor(max_workers=6) as ex:
        unibox_both_sub = np.stack(list(ex.map(rollout_unibox_one, [(raw_hist[i], init_state[i], True, True) for i in sub_idx])), axis=0)
        unibox_air_sub = np.stack(list(ex.map(rollout_unibox_one, [(raw_hist[i], init_state[i], False, True) for i in sub_idx])), axis=0)
        unibox_gnd_sub = np.stack(list(ex.map(rollout_unibox_one, [(raw_hist[i], init_state[i], True, False) for i in sub_idx])), axis=0)

    y_sub = y_pos[sub_idx]
    subsets_all = {
        "all": None,
        "ground": mask_ground,
        "air": mask_air,
        "jumps_transitions": mask_jump,
        "unseen_maps": is_unseen,
    }
    subsets_sub = {
        "all": None,
        "ground": mask_ground[sub_idx],
        "air": mask_air[sub_idx],
        "jumps_transitions": mask_jump[sub_idx],
        "unseen_maps": is_unseen[sub_idx],
    }

    report: dict[str, Any] = {
        "test_windows_total": int(n_tot),
        "analytic_subset_n": int(len(sub_idx)),
        "winner_model": winner_name,
        "full_test": {},
        "head_to_head_subset": {},
        "network_conditions": {},
        "sigma_calibration": {},
        "projectile_hit_rates": {},
    }

    for name, preds in [
        ("const_vel", pred_const_all),
        ("TCN_ResMLP", pred_tcn_all),
        ("GRU_ResMLP", pred_gru_all),
        (f"{winner_name}_noBSP", pred_nobsp_all),
    ]:
        report["full_test"][name] = {k: compute_horizon_metrics(preds, y_pos, m) for k, m in subsets_all.items()}

    for name, preds in [
        ("const_vel", pred_const_all[sub_idx]),
        ("unibox_air_only", unibox_air_sub),
        ("unibox_ground_only", unibox_gnd_sub),
        ("unibox_air_and_ground", unibox_both_sub),
        ("TCN_ResMLP", pred_tcn_all[sub_idx]),
        ("GRU_ResMLP", pred_gru_all[sub_idx]),
        (f"{winner_name}_noBSP", pred_nobsp_all[sub_idx]),
    ]:
        report["head_to_head_subset"][name] = {k: compute_horizon_metrics(preds, y_sub, m) for k, m in subsets_sub.items()}

    p_lat_const = np.linalg.norm(pred_const_all[sub_idx] - y_sub, axis=-1)
    p_lat_uni = np.linalg.norm(unibox_both_sub - y_sub, axis=-1)
    p_lat_win = np.linalg.norm(pred_win_all[sub_idx] - y_sub, axis=-1)
    report["network_conditions"] = {
        "clean_66hz": compute_horizon_metrics(pred_win_all, y_pos),
        "choked_2tick": compute_horizon_metrics(pred_win_choke, y_pos),
        "latency_100ms_subset": {
            "const_vel": {
                "fde_3d_630ms": float(p_lat_const[:, 10].mean()),
                "fde_3d_1170ms": float(p_lat_const[:, 13].mean()),
            },
            "unibox_air_and_ground": {
                "fde_3d_630ms": float(p_lat_uni[:, 10].mean()),
                "fde_3d_1170ms": float(p_lat_uni[:, 13].mean()),
            },
            winner_name: {
                "fde_3d_630ms": float(p_lat_win[:, 10].mean()),
                "fde_3d_1170ms": float(p_lat_win[:, 13].mean()),
            },
        },
    }

    err_xy_all = np.linalg.norm(pred_win_all[..., :2] - y_pos[..., :2], axis=-1)
    cal = {}
    for ki, tk in [(4, "135ms"), (8, "405ms"), (11, "780ms"), (13, "1170ms"), (15, "1800ms")]:
        e = err_xy_all[:, ki]
        s = sxy_win[:, ki]
        cal[tk] = {
            "mean_err_xy": float(e.mean()),
            "mean_pred_sigma_xy": float(s.mean()),
            "ratio_err_over_sigma": float((e / np.maximum(s, 1e-3)).mean()),
            "within_2sigma_frac": float((e <= 2.0 * s).mean()),
            "pearson_corr": float(np.corrcoef(e, s)[0, 1]),
        }
    report["sigma_calibration"] = cal

    report["projectile_hit_rates"] = {
        "const_vel": simulate_projectile_hit_rate(pred_const_all[sub_idx], y_sub, np.random.default_rng(77)),
        "unibox_air_and_ground": simulate_projectile_hit_rate(unibox_both_sub, y_sub, np.random.default_rng(77)),
        "TCN_ResMLP": simulate_projectile_hit_rate(pred_tcn_all[sub_idx], y_sub, np.random.default_rng(77)),
        "GRU_ResMLP": simulate_projectile_hit_rate(pred_gru_all[sub_idx], y_sub, np.random.default_rng(77)),
    }

    out_json = args.ckpt_dir / "eval_report.json"
    out_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Saved evaluation report to {out_json}", flush=True)
    for mname, d in report["head_to_head_subset"].items():
        all_fde = d["all"]["fde_3d"]
        print(
            f"  {mname:24s} | 150ms={all_fde['150ms']:5.1f}  300ms={all_fde['300ms']:5.1f}  "
            f"500ms={all_fde['500ms']:5.1f}  750ms={all_fde['750ms']:5.1f}  "
            f"1000ms={all_fde['1000ms']:5.1f}  1500ms={all_fde['1500ms']:5.1f} hu",
            flush=True,
        )


if __name__ == "__main__":
    main()
