import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np
import torch

from dataset import PINNED_TEST_IDS, build_or_load_cache
from train import load_split_tensors


class MemmapCacheTest(unittest.TestCase):
    def test_streamed_windows_and_pinned_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            cache.mkdir()
            (cache / "test.npz").touch()
            (cache / "split_info.json").write_text(json.dumps({
                "test": {"demos": len(PINNED_TEST_IDS), "demo_ids": list(PINNED_TEST_IDS), "windows": 7}
            }), encoding="utf-8")
            paths = [root / f"{i}.bin" for i in range(3)]
            for path in paths:
                path.touch()
            demos = [
                {"id": str(i), "bin_path": str(path), "meta": {"total_frames": 100}, "map_family": "test"}
                for i, path in enumerate(paths)
            ]

            def extract(path, *args, **kwargs):
                n = int(path.stem) + 1
                return {
                    "x_seq": np.full((n, 32, 22), int(path.stem), dtype=np.float32),
                    "x_ctx": np.zeros((n, 44), dtype=np.float32),
                    "y_pos": np.zeros((n, 16, 3), dtype=np.float32),
                    "y_vel": np.zeros((n, 16, 3), dtype=np.float32),
                    "y_ctrl": np.zeros((n, 16, 5), dtype=np.float32),
                    "init_state": np.zeros((n, 9), dtype=np.float32),
                    "input_source": np.ones(n, dtype=np.uint8),
                    "is_air_traj": np.zeros(n, dtype=np.uint8),
                }

            splits = {"train": demos[:2], "val": demos[2:], "test": []}
            with patch("dataset.build_strict_split", return_value=splits), patch("dataset.extract_windows_from_demo", side_effect=extract):
                stats = build_or_load_cache(cache, force_rebuild=True, memory_mapped=True)
            train = load_split_tensors(cache / "train", torch.device("cpu"))
            val = load_split_tensors(cache / "val", torch.device("cpu"))
            self.assertEqual(stats["train"]["windows"], 3)
            self.assertEqual(stats["test"]["windows"], 7)
            self.assertEqual(train[0][:, 0, 0].tolist(), [0, 1, 1])
            self.assertEqual(val[0][:, 0, 0].tolist(), [2, 2, 2])
            del train, val


if __name__ == "__main__":
    unittest.main()
