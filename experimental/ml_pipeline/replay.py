import struct
from pathlib import Path

import numpy as np
import torch

from models import decode_raw_outputs


RECORD_DTYPE = np.dtype([
    ("x_seq", "<f4", (32, 22)),
    ("x_ctx", "<f4", (44,)),
    ("target", "<f4", (16, 8)),
    ("mask", "<u2"),
    ("pad", "<u2"),
    ("steam_id", "<u4"),
    ("tick", "<i4"),
    ("weight", "<f4"),
])


def load_replay(path: Path, device: torch.device) -> tuple[tuple[torch.Tensor, ...], tuple[torch.Tensor, ...]]:
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError("Replay is shorter than its header")
    magic, version, count, _ = struct.unpack("<4sIII", data[:16])
    if magic != b"SNRB" or version != 1 or not 0 < count <= 2048 or len(data) != 16 + count * RECORD_DTYPE.itemsize:
        raise ValueError("Invalid replay header or size")
    records = np.frombuffer(data, dtype=RECORD_DTYPE, offset=16)
    if any(not np.isfinite(records[key]).all() for key in ("x_seq", "x_ctx", "target", "weight")):
        raise ValueError("Replay contains non-finite values")
    records = records[records["mask"] != 0]
    players = np.unique(records["steam_id"])
    if len(players) < 2:
        raise ValueError("Replay needs at least two distinct players for a held-out split")
    rng = np.random.default_rng(2026)
    shuffled = rng.permutation(players)
    n_val = min(len(players) - 1, max(1, round(len(players) * 0.2)))
    val = np.isin(records["steam_id"], shuffled[:n_val])

    def tensors(selection: np.ndarray) -> tuple[torch.Tensor, ...]:
        subset = records[selection]
        mask = ((subset["mask"][:, None] >> np.arange(16)) & 1).astype(np.float32)
        return tuple(torch.as_tensor(array.copy(), device=device) for array in (
            subset["x_seq"], subset["x_ctx"], subset["target"][:, :, :5], mask,
            np.clip(subset["weight"], 0.5, 4.0),
        ))

    return tensors(~val), tensors(val)


def control_loss(model: torch.nn.Module, tensors: tuple[torch.Tensor, ...]) -> torch.Tensor:
    x_seq, x_ctx, target, mask, weight = tensors
    decoded = decode_raw_outputs(model(x_seq, x_ctx).float())
    predicted = torch.stack([
        decoded["wish_fwd"], decoded["wish_side"], decoded["turn_rate"] / 15.0,
        decoded["p_jump"], decoded["p_duck"],
    ], dim=-1)
    obs_weight = mask * weight[:, None]
    return (((predicted - target) ** 2).mean(dim=-1) * obs_weight).sum() / obs_weight.sum().clamp_min(1.0)


def evaluate_replay(model: torch.nn.Module, tensors: tuple[torch.Tensor, ...]) -> float:
    model.eval()
    with torch.no_grad():
        return float(control_loss(model, tensors).item())
