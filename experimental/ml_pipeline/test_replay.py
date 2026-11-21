import struct
import tempfile
import unittest
from pathlib import Path

import numpy as np
import torch

from replay import RECORD_DTYPE, control_loss, load_replay


class ZeroModel(torch.nn.Module):
    def forward(self, x_seq, x_ctx):
        return torch.zeros((len(x_seq), 16, 8))


class ReplayTest(unittest.TestCase):
    def test_split_and_observed_controls(self):
        records = np.zeros(4, dtype=RECORD_DTYPE)
        records["steam_id"] = [1, 1, 2, 2]
        records["x_ctx"][:, 0] = records["steam_id"]
        records["mask"] = 1
        records["weight"] = 1
        records["target"][:, :, 3:5] = 0.5
        records["target"][:, 1, 0] = 1
        header = struct.pack("<4sIII", b"SNRB", 1, len(records), 0)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "replay.bin"
            path.write_bytes(header + records.tobytes())
            train, val = load_replay(path, torch.device("cpu"))
            path.write_bytes(header)
            with self.assertRaises(ValueError):
                load_replay(path, torch.device("cpu"))

        self.assertTrue(set(train[1][:, 0].tolist()).isdisjoint(val[1][:, 0].tolist()))
        self.assertEqual(control_loss(ZeroModel(), train).item(), 0.0)
        train[2][:, 0, 0] = 1
        self.assertGreater(control_loss(ZeroModel(), train).item(), 0.0)


if __name__ == "__main__":
    unittest.main()
