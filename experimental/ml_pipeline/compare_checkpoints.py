import argparse
import json
from pathlib import Path

import numpy as np
import torch

from evaluate import compute_horizon_metrics, predict_neural
from models import DifferentiableSourceRollout, GRUResMLP, TCNResMLP
from replay import evaluate_replay, load_replay


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--test", type=Path, default=Path("ml_pipeline/data/cache/test.npz"))
    parser.add_argument("--replay", type=Path)
    args = parser.parse_args()

    data = np.load(args.test)
    x_seq = np.asarray(data["x_seq"], dtype=np.float32)
    x_ctx = np.asarray(data["x_ctx"], dtype=np.float32)
    init = np.asarray(data["init_state"], dtype=np.float32)
    target = np.asarray(data["y_pos"], dtype=np.float32)
    jump = np.any(data["y_ctrl"][..., 3] > 0.5, axis=1)
    ground = init[:, 4] > 0.5
    masks = {"all": None, "ground": ground & ~jump, "air": ~ground}

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    rollout = torch.jit.script(DifferentiableSourceRollout().to(device))
    replay_val = load_replay(args.replay, device)[1] if args.replay else None
    results = {}
    for label, path in (("baseline", args.baseline), ("candidate", args.candidate)):
        checkpoint = torch.load(path, map_location=device, weights_only=False)
        model = (GRUResMLP() if checkpoint["arch"] == "gru" else TCNResMLP()).to(device)
        model.load_state_dict(checkpoint["model"])
        predicted, _, _ = predict_neural(model, rollout, x_seq, x_ctx, init, device)
        results[label] = {name: compute_horizon_metrics(predicted, target, mask) for name, mask in masks.items()}
        if replay_val is not None:
            results[label]["replay_held_out_control_mse"] = evaluate_replay(model, replay_val)
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
