import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader

from dataset import HORIZON_KNOTS, TrajectoryDataset, build_or_load_cache
from models import (
    DifferentiableSourceRollout,
    GRUResMLP,
    TCNResMLP,
    decode_raw_outputs,
    export_weights_bin,
)


def compute_losses(
    decoded: dict[str, torch.Tensor],
    pred_pos: torch.Tensor,
    pred_vel: torch.Tensor,
    y_pos: torch.Tensor,
    y_vel: torch.Tensor,
    y_ctrl: torch.Tensor,
    input_source: torch.Tensor,
    knot_weights: torch.Tensor,
) -> tuple[torch.Tensor, dict[str, float]]:
    dx = pred_pos[..., 0] - y_pos[..., 0]
    dy = pred_pos[..., 1] - y_pos[..., 1]
    dz = pred_pos[..., 2] - y_pos[..., 2]
    err_xy_sq = dx * dx + dy * dy
    err_z_sq = dz * dz
    err_xy = torch.sqrt(err_xy_sq + 1e-6)

    sig_xy = decoded["sigma_xy"]
    sig_z = decoded["sigma_z"]

    nll_xy = 0.5 * (err_xy_sq / (sig_xy * sig_xy)) + 2.0 * torch.log(sig_xy)
    nll_z = 0.5 * (err_z_sq / (sig_z * sig_z)) + torch.log(sig_z)
    pos_l1 = F.smooth_l1_loss(pred_pos, y_pos, reduction="none", beta=8.0).mean(dim=-1)
    vel_l1 = F.smooth_l1_loss(pred_vel, y_vel, reduction="none", beta=16.0).mean(dim=-1)

    loss_nll = ((nll_xy + 0.5 * nll_z) * knot_weights).mean()
    loss_pos = (pos_l1 * knot_weights).mean()
    loss_vel = (vel_l1 * knot_weights).mean()

    src_w = torch.where(input_source > 0.5, 2.0, 0.5).unsqueeze(1)
    gt_wf = y_ctrl[..., 0]
    gt_ws = y_ctrl[..., 1]
    gt_tr = y_ctrl[..., 2]
    gt_jump = y_ctrl[..., 3]
    gt_duck = y_ctrl[..., 4]

    pw_f = decoded["wish_fwd"]
    pw_s = decoded["wish_side"]
    wish_l1 = (torch.abs(pw_f - gt_wf) + torch.abs(pw_s - gt_ws)) * src_w

    gt_mag = torch.sqrt(gt_wf * gt_wf + gt_ws * gt_ws + 1e-8)
    p_mag = torch.sqrt(pw_f * pw_f + pw_s * pw_s + 1e-8)
    active_m = (gt_mag > 0.1).float()
    cos_sim = (pw_f * gt_wf + pw_s * gt_ws) / (p_mag * gt_mag)
    wish_cos = ((1.0 - cos_sim) * active_m) * src_w

    turn_loss = F.smooth_l1_loss(decoded["turn_rate"], gt_tr, reduction="none", beta=1.0) * src_w
    jump_bce = F.binary_cross_entropy_with_logits(
        decoded["jump_logit"], gt_jump, pos_weight=torch.tensor(4.0, device=y_pos.device), reduction="none"
    ) * src_w
    duck_bce = F.binary_cross_entropy_with_logits(decoded["duck_logit"], gt_duck, reduction="none") * src_w

    commit_target = (err_xy.detach() < 48.0).float()
    commit_bce = F.binary_cross_entropy_with_logits(decoded["commit_logit"], commit_target)

    loss_ctrl = (
        0.5 * wish_l1.mean()
        + 0.35 * wish_cos.mean()
        + 0.15 * turn_loss.mean()
        + 0.4 * jump_bce.mean()
        + 0.15 * duck_bce.mean()
        + 0.15 * commit_bce
    )

    total = loss_nll + 0.08 * loss_pos + 0.02 * loss_vel + loss_ctrl

    with torch.no_grad():
        ade_150 = err_xy[:, 4].mean().item()
        ade_500 = err_xy[:, 8:10].mean().item()
        ade_1000 = err_xy[:, 12].mean().item()
        fde_1500 = err_xy[:, 14].mean().item()

    metrics = {
        "loss": float(total.item()),
        "nll": float(loss_nll.item()),
        "pos_l1": float(loss_pos.item()),
        "ctrl": float(loss_ctrl.item()),
        "xy_9": ade_150,
        "xy_30": ade_500,
        "xy_64": ade_1000,
        "xy_96": fde_1500,
    }
    return total, metrics


def run_eval_epoch(
    model: nn.Module,
    rollout: torch.jit.ScriptModule,
    val_tensors: tuple[torch.Tensor, ...],
    batch_size: int,
    knot_weights: torch.Tensor,
) -> dict[str, float]:
    model.eval()
    sums: dict[str, float] = {}
    count = 0
    xs_all, xc_all, yp_all, yv_all, yc_all, ist_all, isrc_all = val_tensors
    n = xs_all.shape[0]
    with torch.no_grad():
        for i in range(0, n, batch_size):
            x_seq = xs_all[i : i + batch_size]
            x_ctx = xc_all[i : i + batch_size]
            y_pos = yp_all[i : i + batch_size]
            y_vel = yv_all[i : i + batch_size]
            y_ctrl = yc_all[i : i + batch_size]
            init_st = ist_all[i : i + batch_size]
            isrc = isrc_all[i : i + batch_size]

            raw = model(x_seq, x_ctx)
            dec = decode_raw_outputs(raw)
            p_pos, p_vel = rollout(
                dec["wish_fwd"], dec["wish_side"], dec["turn_rate"], dec["p_jump"], dec["p_duck"], init_st, True
            )
            _, metrics = compute_losses(dec, p_pos, p_vel, y_pos, y_vel, y_ctrl, isrc, knot_weights)
            bs = x_seq.shape[0]
            for k, v in metrics.items():
                sums[k] = sums.get(k, 0.0) + v * bs
            count += bs
    return {k: v / max(count, 1) for k, v in sums.items()}


def train_model(
    arch: str,
    train_tensors: tuple[torch.Tensor, ...],
    val_tensors: tuple[torch.Tensor, ...],
    batch_size: int,
    epochs: int,
    lr: float,
    ckpt_dir: Path,
    device: torch.device,
) -> dict:
    model: nn.Module = TCNResMLP() if arch == "tcn" else GRUResMLP()
    model = model.to(device)
    rollout = torch.jit.script(DifferentiableSourceRollout().to(device))

    knot_t = torch.tensor(HORIZON_KNOTS, dtype=torch.float32, device=device)
    knot_weights = (1.0 / torch.sqrt(knot_t)).unsqueeze(0)
    knot_weights = knot_weights / knot_weights.mean()

    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=epochs, eta_min=lr * 0.05)

    best_val_pos = float("inf")
    best_pt = ckpt_dir / f"{arch}_best.pt"
    history = []

    xs_all, xc_all, yp_all, yv_all, yc_all, ist_all, isrc_all = train_tensors
    n_train = xs_all.shape[0]

    for ep in range(1, epochs + 1):
        t0 = time.perf_counter()
        model.train()
        tr_sums: dict[str, float] = {}
        tr_cnt = 0

        perm = torch.randperm(n_train, device=device)
        for i in range(0, n_train - batch_size + 1, batch_size):
            idx = perm[i : i + batch_size]
            x_seq = xs_all[idx]
            x_ctx = xc_all[idx]
            y_pos = yp_all[idx]
            y_vel = yv_all[idx]
            y_ctrl = yc_all[idx]
            init_st = ist_all[idx]
            isrc = isrc_all[idx]

            opt.zero_grad(set_to_none=True)
            raw = model(x_seq, x_ctx)
            dec = decode_raw_outputs(raw)
            p_pos, p_vel = rollout(
                dec["wish_fwd"], dec["wish_side"], dec["turn_rate"], dec["p_jump"], dec["p_duck"], init_st, False
            )
            loss, metrics = compute_losses(dec, p_pos, p_vel, y_pos, y_vel, y_ctrl, isrc, knot_weights)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 2.0)
            opt.step()

            bs = x_seq.shape[0]
            for k, v in metrics.items():
                tr_sums[k] = tr_sums.get(k, 0.0) + v * bs
            tr_cnt += bs

        sched.step()
        tr_metrics = {k: v / max(tr_cnt, 1) for k, v in tr_sums.items()}
        val_metrics = run_eval_epoch(model, rollout, val_tensors, batch_size * 2, knot_weights)
        elapsed = time.perf_counter() - t0

        score = val_metrics["xy_30"] + 0.5 * val_metrics["xy_64"]
        if score < best_val_pos:
            best_val_pos = score
            torch.save({"model": model.state_dict(), "arch": arch, "epoch": ep, "val": val_metrics}, best_pt)

        history.append({"epoch": ep, "train": tr_metrics, "val": val_metrics, "sec": elapsed})
        print(
            f"[{arch.upper()} ep {ep:02d}/{epochs}] "
            f"tr_loss={tr_metrics['loss']:.3f} tr_xy64={tr_metrics['xy_64']:.1f} | "
            f"val_loss={val_metrics['loss']:.3f} val_xy9={val_metrics['xy_9']:.1f} "
            f"val_xy30={val_metrics['xy_30']:.1f} val_xy64={val_metrics['xy_64']:.1f} "
            f"val_xy96={val_metrics['xy_96']:.1f} ({elapsed:.1f}s)",
            flush=True,
        )

    return {"arch": arch, "best_ckpt": str(best_pt), "best_score": best_val_pos, "history": history}


def load_split_tensors(npz_path: Path, device: torch.device) -> tuple[torch.Tensor, ...]:
    d = np.load(npz_path)
    return (
        torch.from_numpy(d["x_seq"].astype(np.float32)).to(device),
        torch.from_numpy(d["x_ctx"].astype(np.float32)).to(device),
        torch.from_numpy(d["y_pos"].astype(np.float32)).to(device),
        torch.from_numpy(d["y_vel"].astype(np.float32)).to(device),
        torch.from_numpy(d["y_ctrl"].astype(np.float32)).to(device),
        torch.from_numpy(d["init_state"].astype(np.float32)).to(device),
        torch.from_numpy(d["input_source"].astype(np.float32)).to(device),
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache-dir", type=Path, default=Path("ml_pipeline/data/cache"))
    parser.add_argument("--ckpt-dir", type=Path, default=Path("ml_pipeline/checkpoints"))
    parser.add_argument("--epochs", type=int, default=15)
    parser.add_argument("--batch-size", type=int, default=1536)
    parser.add_argument("--lr", type=float, default=1.5e-3)
    parser.add_argument("--arch", choices=["tcn", "gru", "both"], default="both")
    args = parser.parse_args()

    args.ckpt_dir.mkdir(parents=True, exist_ok=True)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}", flush=True)

    split_info = build_or_load_cache(args.cache_dir)
    print("Split summary:", json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "demo_ids"} for k, v in split_info.items()}, indent=2), flush=True)

    train_tensors = load_split_tensors(args.cache_dir / "train.npz", device)
    val_tensors = load_split_tensors(args.cache_dir / "val.npz", device)

    arches = ["tcn", "gru"] if args.arch == "both" else [args.arch]
    results = {}
    for a in arches:
        results[a] = train_model(a, train_tensors, val_tensors, args.batch_size, args.epochs, args.lr, args.ckpt_dir, device)

    best_arch = min(results, key=lambda k: results[k]["best_score"])
    best_ckpt = torch.load(results[best_arch]["best_ckpt"], map_location="cpu")
    best_model = TCNResMLP() if best_arch == "tcn" else GRUResMLP()
    best_model.load_state_dict(best_ckpt["model"])
    best_model.eval()
    export_weights_bin(best_model, args.ckpt_dir / "strafe_net_v1.bin")

    test_data = np.load(args.cache_dir / "test.npz", mmap_mode="r")
    n_vec = min(64, len(test_data["x_seq"]))
    tv_seq = torch.from_numpy(np.array(test_data["x_seq"][:n_vec], dtype=np.float32))
    tv_ctx = torch.from_numpy(np.array(test_data["x_ctx"][:n_vec], dtype=np.float32))
    with torch.no_grad():
        tv_out = best_model(tv_seq, tv_ctx).numpy().astype(np.float32)

    with open(args.ckpt_dir / "test_vectors.bin", "wb") as f:
        import struct
        f.write(b"TVEC")
        f.write(struct.pack("<IIII", n_vec, 32 * 22, 44, 16 * 8))
        f.write(tv_seq.numpy().astype(np.float32).tobytes())
        f.write(tv_ctx.numpy().astype(np.float32).tobytes())
        f.write(tv_out.tobytes())

    (args.ckpt_dir / "train_summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"Best arch: {best_arch} -> exported strafe_net_v1.bin and test_vectors.bin", flush=True)


if __name__ == "__main__":
    main()
