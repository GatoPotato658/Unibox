import argparse
import struct
from pathlib import Path

import numpy as np
import torch


def _conv1d_to_im2col(w: torch.Tensor) -> np.ndarray:
    return w.detach().cpu().permute(2, 1, 0).contiguous().numpy().astype(np.float32).reshape(-1)


def _linear_to_rowmajor(w: torch.Tensor) -> np.ndarray:
    return w.detach().cpu().t().contiguous().numpy().astype(np.float32).reshape(-1)


def _vec(t: torch.Tensor) -> np.ndarray:
    return t.detach().cpu().contiguous().numpy().astype(np.float32).reshape(-1)


def _permute_gru_rzn_to_zrn(t: torch.Tensor, hidden: int = 128) -> torch.Tensor:
    r, z, n = t[0:hidden], t[hidden:2 * hidden], t[2 * hidden:3 * hidden]
    return torch.cat([z, r, n], dim=0)


def export_checkpoint_to_snv1(ckpt_path: Path, out_path: Path) -> dict:
    ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=False)
    if isinstance(ckpt, dict) and "model" in ckpt:
        sd = ckpt["model"]
        arch_hint = ckpt.get("arch", "")
    elif isinstance(ckpt, dict) and "model_state" in ckpt:
        sd = ckpt["model_state"]
        arch_hint = ckpt.get("arch", "")
    elif isinstance(ckpt, dict) and "state_dict" in ckpt:
        sd = ckpt["state_dict"]
        arch_hint = ckpt.get("arch", "")
    else:
        sd = ckpt
        arch_hint = ""

    is_gru = ("gru.weight_ih_l0" in sd) or ("GRU" in str(arch_hint))
    arch_type = 1 if is_gru else 0

    bb_parts = [
        _linear_to_rowmajor(sd["ctx_fc1.weight"]),
        _vec(sd["ctx_fc1.bias"]),
        _linear_to_rowmajor(sd["ctx_fc2.weight"]),
        _vec(sd["ctx_fc2.bias"]),
    ]
    if is_gru:
        bb_parts.extend([
            _conv1d_to_im2col(sd["conv1.conv.weight"]),
            _vec(sd["conv1.conv.bias"]),
            _linear_to_rowmajor(_permute_gru_rzn_to_zrn(sd["gru.weight_ih_l0"], 128)),
            _vec(_permute_gru_rzn_to_zrn(sd["gru.bias_ih_l0"], 128)),
            _linear_to_rowmajor(_permute_gru_rzn_to_zrn(sd["gru.weight_hh_l0"], 128)),
            _vec(_permute_gru_rzn_to_zrn(sd["gru.bias_hh_l0"], 128)),
        ])
    else:
        bb_parts.extend([
            _conv1d_to_im2col(sd["conv1.conv.weight"]),
            _vec(sd["conv1.conv.bias"]),
            _conv1d_to_im2col(sd["conv2.conv.weight"]),
            _vec(sd["conv2.conv.bias"]),
            _conv1d_to_im2col(sd["conv3.conv.weight"]),
            _vec(sd["conv3.conv.bias"]),
        ])

    plastic_parts = [
        _linear_to_rowmajor(sd["fuse_fc1.weight"]),
        _vec(sd["fuse_fc1.bias"]),
        _linear_to_rowmajor(sd["fuse_fc2.weight"]),
        _vec(sd["fuse_fc2.bias"]),
        _linear_to_rowmajor(sd["fuse_fc3.weight"]),
        _vec(sd["fuse_fc3.bias"]),
        _linear_to_rowmajor(sd["head.weight"]),
        _vec(sd["head.bias"]),
    ]

    backbone = np.concatenate(bb_parts).astype(np.float32)
    plastic = np.concatenate(plastic_parts).astype(np.float32)
    expected_bb = 129920 if is_gru else 129408
    assert backbone.size == expected_bb, f"backbone size {backbone.size} != {expected_bb}"
    assert plastic.size == 131712, f"plastic size {plastic.size} != 131712"

    header = struct.pack(
        "<4s15I",
        b"SNV1",
        1,
        arch_type,
        32,
        22,
        44,
        128,
        256,
        16,
        8,
        backbone.size,
        plastic.size,
        0,
        0,
        0,
        0,
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(backbone.tobytes())
        f.write(plastic.tobytes())
        f.write(plastic.tobytes())

    return {
        "arch_type": arch_type,
        "backbone_params": int(backbone.size),
        "plastic_params": int(plastic.size),
        "bytes": 64 + int(backbone.nbytes) + 2 * int(plastic.nbytes),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ckpt", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    info = export_checkpoint_to_snv1(args.ckpt, args.out)
    print(f"Exported {args.ckpt} -> {args.out}: {info}")


if __name__ == "__main__":
    main()
