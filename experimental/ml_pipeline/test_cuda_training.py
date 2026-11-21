import math
import tempfile
import unittest
from pathlib import Path

import torch

from models import TCNResMLP
from train import train_model


@unittest.skipUnless(torch.cuda.is_available(), "CUDA is unavailable")
class CudaTrainingTest(unittest.TestCase):
    def test_amp_with_casual_replay(self):
        device = torch.device("cuda")
        n = 4
        init = torch.zeros((n, 9))
        init[:, 3] = 300
        init[:, 4] = 1
        data = (
            torch.zeros((n, 32, 22)), torch.zeros((n, 44)),
            torch.zeros((n, 16, 3)), torch.zeros((n, 16, 3)),
            torch.zeros((n, 16, 5)), init, torch.zeros(n), torch.zeros(n),
        )
        replay = (
            torch.zeros((n, 32, 22), device=device), torch.zeros((n, 44), device=device),
            torch.zeros((n, 16, 5), device=device), torch.ones((n, 16), device=device),
            torch.ones(n, device=device),
        )
        torch.manual_seed(2026)
        initial = TCNResMLP().head.weight.detach().clone()
        torch.manual_seed(2026)
        with tempfile.TemporaryDirectory() as tmp:
            result = train_model("tcn", data, data, n, 1, 1e-3, Path(tmp), device, replay_train=replay, replay_val=replay, use_amp=True)
            self.assertTrue(Path(result["best_ckpt"]).is_file())
            self.assertTrue(math.isfinite(result["best_score"]))
            self.assertTrue(math.isfinite(result["history"][0]["val"]["replay_control_mse"]))
            trained = torch.load(result["best_ckpt"], map_location="cpu", weights_only=False)["model"]["head.weight"]
            self.assertFalse(torch.equal(initial, trained))


if __name__ == "__main__":
    unittest.main()
