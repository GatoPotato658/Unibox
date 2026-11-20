import math
import struct
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HORIZON_KNOTS = [1, 2, 4, 6, 9, 12, 16, 21, 27, 34, 42, 52, 64, 78, 96, 120]
MAX_HORIZON = 120
TICK_INTERVAL = 0.015
GRAVITY = 800.0
JUMP_VEL = 289.0
SV_ACCEL = 10.0
SV_AIRACCEL = 10.0
SV_FRICTION = 4.0
SV_STOPSPEED = 100.0
AIR_CAP = 30.0
DUCK_SCALE = 1.0 / 3.0


def build_knot_interp_matrix() -> torch.Tensor:
    knots = HORIZON_KNOTS
    mat = torch.zeros(MAX_HORIZON, len(knots), dtype=torch.float32)
    for t in range(1, MAX_HORIZON + 1):
        if t <= knots[0]:
            mat[t - 1, 0] = 1.0
            continue
        if t >= knots[-1]:
            mat[t - 1, -1] = 1.0
            continue
        for k in range(len(knots) - 1):
            if knots[k] <= t <= knots[k + 1]:
                w = float(t - knots[k]) / float(knots[k + 1] - knots[k])
                mat[t - 1, k] = 1.0 - w
                mat[t - 1, k + 1] = w
                break
    return mat


class CausalConv1d(nn.Module):
    def __init__(self, in_channels: int, out_channels: int, kernel_size: int = 3, dilation: int = 1):
        super().__init__()
        self.pad_left = (kernel_size - 1) * dilation
        self.conv = nn.Conv1d(in_channels, out_channels, kernel_size, dilation=dilation)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        if self.pad_left > 0:
            x = F.pad(x, (self.pad_left, 0))
        return self.conv(x)


class TCNResMLP(nn.Module):
    def __init__(self, seq_dim: int = 22, ctx_dim: int = 44, hidden: int = 128, num_knots: int = 16):
        super().__init__()
        self.arch_name = "TCN_ResMLP"
        self.conv1 = CausalConv1d(seq_dim, hidden, kernel_size=3, dilation=1)
        self.conv2 = CausalConv1d(hidden, hidden, kernel_size=3, dilation=2)
        self.conv3 = CausalConv1d(hidden, hidden, kernel_size=3, dilation=4)

        self.ctx_fc1 = nn.Linear(ctx_dim, hidden)
        self.ctx_fc2 = nn.Linear(hidden, hidden)

        self.fuse_fc1 = nn.Linear(hidden * 2, hidden * 2)
        self.fuse_fc2 = nn.Linear(hidden * 2, hidden)
        self.fuse_fc3 = nn.Linear(hidden, hidden)
        self.head = nn.Linear(hidden, num_knots * 8)
        with torch.no_grad():
            b = self.head.bias.view(num_knots, 8)
            b[:, 5] = 3.2
            b[:, 6] = 2.8

    def forward(self, x_seq: torch.Tensor, x_ctx: torch.Tensor) -> torch.Tensor:
        h = x_seq.transpose(1, 2)
        h = F.silu(self.conv1(h))
        h = h + F.silu(self.conv2(h))
        h = h + F.silu(self.conv3(h))
        seq_feat = h[:, :, -1]

        c = F.silu(self.ctx_fc1(x_ctx))
        c = F.silu(self.ctx_fc2(c))

        z = torch.cat([seq_feat, c], dim=-1)
        z = F.silu(self.fuse_fc1(z))
        z = F.silu(self.fuse_fc2(z))
        z = F.silu(self.fuse_fc3(z))
        out = self.head(z)
        return out.view(-1, 16, 8)


class GRUResMLP(nn.Module):
    def __init__(self, seq_dim: int = 22, ctx_dim: int = 44, hidden: int = 128, num_knots: int = 16):
        super().__init__()
        self.arch_name = "GRU_ResMLP"
        self.conv1 = CausalConv1d(seq_dim, hidden, kernel_size=3, dilation=1)
        self.gru = nn.GRU(hidden, hidden, batch_first=True)

        self.ctx_fc1 = nn.Linear(ctx_dim, hidden)
        self.ctx_fc2 = nn.Linear(hidden, hidden)

        self.fuse_fc1 = nn.Linear(hidden * 2, hidden * 2)
        self.fuse_fc2 = nn.Linear(hidden * 2, hidden)
        self.fuse_fc3 = nn.Linear(hidden, hidden)
        self.head = nn.Linear(hidden, num_knots * 8)
        with torch.no_grad():
            b = self.head.bias.view(num_knots, 8)
            b[:, 5] = 3.2
            b[:, 6] = 2.8

    def forward(self, x_seq: torch.Tensor, x_ctx: torch.Tensor) -> torch.Tensor:
        h = F.silu(self.conv1(x_seq.transpose(1, 2))).transpose(1, 2)
        _, h_last = self.gru(h)
        seq_feat = h_last.squeeze(0)

        c = F.silu(self.ctx_fc1(x_ctx))
        c = F.silu(self.ctx_fc2(c))

        z = torch.cat([seq_feat, c], dim=-1)
        z = F.silu(self.fuse_fc1(z))
        z = F.silu(self.fuse_fc2(z))
        z = F.silu(self.fuse_fc3(z))
        out = self.head(z)
        return out.view(-1, 16, 8)


def decode_raw_outputs(raw: torch.Tensor) -> dict[str, torch.Tensor]:
    wish_fwd = torch.tanh(raw[..., 0])
    wish_side = torch.tanh(raw[..., 1])
    turn_rate = 15.0 * torch.tanh(raw[..., 2])
    jump_logit = raw[..., 3]
    duck_logit = raw[..., 4]
    p_jump = torch.sigmoid(jump_logit)
    p_duck = torch.sigmoid(duck_logit)
    sigma_xy = F.softplus(raw[..., 5]) + 1.0
    sigma_z = F.softplus(raw[..., 6]) + 1.0
    commit_logit = raw[..., 7]
    commit_prob = torch.sigmoid(commit_logit)
    return {
        "wish_fwd": wish_fwd,
        "wish_side": wish_side,
        "turn_rate": turn_rate,
        "jump_logit": jump_logit,
        "duck_logit": duck_logit,
        "p_jump": p_jump,
        "p_duck": p_duck,
        "sigma_xy": sigma_xy,
        "sigma_z": sigma_z,
        "commit_logit": commit_logit,
        "commit_prob": commit_prob,
    }


class DifferentiableSourceRollout(nn.Module):
    def __init__(self):
        super().__init__()
        self.register_buffer("w_interp", build_knot_interp_matrix())
        self.knot_indices = [k - 1 for k in HORIZON_KNOTS]

    def forward(
        self,
        wf_k: torch.Tensor,
        ws_k: torch.Tensor,
        tr_k: torch.Tensor,
        pj_k: torch.Tensor,
        pd_k: torch.Tensor,
        init_state: torch.Tensor,
        deterministic_discrete: bool = False,
    ) -> tuple[torch.Tensor, torch.Tensor]:
        tick_dt = 0.015
        gravity = 800.0
        jump_vel = 289.0
        sv_accel = 10.0
        sv_airaccel = 10.0
        sv_friction = 4.0
        sv_stopspeed = 100.0
        air_cap = 30.0
        duck_scale = 1.0 / 3.0

        ctrl_seq = torch.stack([wf_k, ws_k, tr_k, pj_k, pd_k], dim=-1)
        dense = torch.einsum("tk,bkc->btc", self.w_interp, ctrl_seq)

        bsz = init_state.shape[0]
        device = init_state.device
        dtype = init_state.dtype

        vx = init_state[:, 0]
        vy = init_state[:, 1]
        vz = init_state[:, 2]
        maxspeed = torch.clamp(init_state[:, 3], min=100.0, max=520.0)
        g_prob = init_state[:, 4]
        ground_dist0 = torch.clamp(init_state[:, 8], min=0.0, max=512.0)

        px = torch.zeros(bsz, device=device, dtype=dtype)
        py = torch.zeros(bsz, device=device, dtype=dtype)
        pz = torch.zeros(bsz, device=device, dtype=dtype)
        yaw_deg = torch.zeros(bsz, device=device, dtype=dtype)

        ground_floor_z = torch.where(g_prob > 0.5, torch.zeros_like(pz), -ground_dist0)
        half_grav = 0.5 * gravity * tick_dt
        deg_to_rad = 3.141592653589793 / 180.0
        accel_dt = sv_accel * tick_dt
        airaccel_dt = sv_airaccel * tick_dt

        pos_out: list[torch.Tensor] = []
        vel_out: list[torch.Tensor] = []

        for t in range(120):
            wf = dense[:, t, 0]
            ws = dense[:, t, 1]
            tr = dense[:, t, 2]
            pj = dense[:, t, 3]
            pd = dense[:, t, 4]

            if deterministic_discrete:
                j_gate = ((pj > 0.5) & (g_prob > 0.5)).to(dtype)
                d_val = (pd > 0.5).to(dtype)
            else:
                j_gate = g_prob * torch.sigmoid((pj - 0.5) * 10.0)
                d_val = pd

            vz = vz + j_gate * jump_vel
            g_curr = torch.clamp(g_prob * (1.0 - j_gate), min=0.0, max=1.0)

            vz = vz - (1.0 - g_curr) * half_grav

            yaw_rad = yaw_deg * deg_to_rad
            cy = torch.cos(yaw_rad)
            sy = torch.sin(yaw_rad)
            wx = cy * wf + sy * ws
            wy = sy * wf - cy * ws
            wnorm = torch.sqrt(wx * wx + wy * wy + 1e-8)
            wmag = torch.clamp(wnorm, max=1.0)
            wdx = wx / wnorm
            wdy = wy / wnorm

            eff_max = maxspeed * (1.0 - (1.0 - duck_scale) * d_val)

            sp_g = torch.sqrt(vx * vx + vy * vy + 1e-8)
            ctrl_g = torch.clamp(sp_g, min=sv_stopspeed)
            drop_g = ctrl_g * sv_friction * tick_dt
            scale_g = torch.where(sp_g > 0.1, torch.clamp(sp_g - drop_g, min=0.0) / sp_g, torch.ones_like(sp_g))
            vfx = vx * scale_g
            vfy = vy * scale_g

            wish_spd_g = eff_max * wmag
            cur_g = vfx * wdx + vfy * wdy
            add_g = torch.clamp(wish_spd_g - cur_g, min=0.0)
            acc_g = torch.minimum(accel_dt * wish_spd_g, add_g)
            vgx = vfx + wdx * acc_g
            vgy = vfy + wdy * acc_g
            sp_g2 = torch.sqrt(vgx * vgx + vgy * vgy + 1e-8)
            clamp_g = torch.minimum(torch.ones_like(sp_g2), eff_max / sp_g2)
            vgx = vgx * clamp_g
            vgy = vgy * clamp_g

            wish_spd_a = maxspeed * wmag
            capped_a = torch.clamp(wish_spd_a, max=air_cap)
            cur_a = vx * wdx + vy * wdy
            add_a = torch.clamp(capped_a - cur_a, min=0.0)
            acc_a = torch.minimum(airaccel_dt * wish_spd_a, add_a)
            vax = vx + wdx * acc_a
            vay = vy + wdy * acc_a

            vx = g_curr * vgx + (1.0 - g_curr) * vax
            vy = g_curr * vgy + (1.0 - g_curr) * vay

            px = px + vx * tick_dt
            py = py + vy * tick_dt
            pz = pz + vz * tick_dt

            vz = vz - (1.0 - g_curr) * half_grav
            if deterministic_discrete:
                landed = (g_curr < 0.5) & (pz <= ground_floor_z) & (vz <= 0.0)
                pz = torch.where(landed, ground_floor_z, pz)
                vz = torch.where(landed | (g_curr > 0.5), torch.zeros_like(vz), vz)
                g_prob = torch.where(landed, torch.ones_like(g_curr), g_curr)
            else:
                falling = (vz <= 0.0).to(dtype)
                land_gate = (1.0 - g_curr) * falling * torch.sigmoid((ground_floor_z - pz) * 0.5)
                pz = torch.where(g_curr > 0.5, ground_floor_z, pz)
                vz = vz * (1.0 - g_curr) * (1.0 - land_gate)
                g_prob = torch.clamp(g_curr + land_gate, min=0.0, max=1.0)

            yaw_deg = yaw_deg + tr

            if t in self.knot_indices:
                pos_out.append(torch.stack([px, py, pz], dim=-1))
                vel_out.append(torch.stack([vx, vy, vz], dim=-1))

        return torch.stack(pos_out, dim=1), torch.stack(vel_out, dim=1)


def export_weights_bin(model: nn.Module, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    state = model.state_dict()
    arch_id = 1 if getattr(model, "arch_name", "") == "TCN_ResMLP" else 2
    with open(out_path, "wb") as f:
        f.write(b"SNW1")
        f.write(struct.pack("<II", arch_id, len(state)))
        for name, tensor in state.items():
            name_b = name.encode("utf-8")
            arr = tensor.detach().cpu().contiguous().numpy().astype(np.float32)
            f.write(struct.pack("<II", len(name_b), arr.ndim))
            f.write(name_b)
            for dim in arr.shape:
                f.write(struct.pack("<I", dim))
            f.write(arr.tobytes())
