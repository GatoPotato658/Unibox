import json
import tempfile
import unittest
from pathlib import Path

from dataset import PINNED_TEST_IDS, PINNED_VAL_IDS, build_strict_split


class SplitTest(unittest.TestCase):
    def test_pinned_test_and_overlapping_recordings(self):
        demos = [
            {
                "id": demo_id,
                "local_path": f"{demo_id}.dem",
                "map": "test_map",
                "bsp_path": "test_map.bsp",
                "map_family": "test_map",
                "time": 100_000 + i * 20_000,
                "sha256": demo_id,
                "is_pov": True,
            }
            for i, demo_id in enumerate(sorted(PINNED_TEST_IDS))
        ]
        test = demos[0]
        demos.extend([
            {**test, "id": "near_test", "local_path": "near_test.dem", "time": test["time"] + 60, "sha256": "near"},
            {**test, "id": "same_hash", "local_path": "same_hash.dem", "map_family": "other_map"},
            {**test, "id": next(iter(PINNED_VAL_IDS)), "local_path": "val.dem", "map_family": "val_map", "time": 500_000, "sha256": "val"},
            {**test, "id": "near_val", "local_path": "near_val.dem", "map_family": "val_map", "time": 500_060, "sha256": "train"},
            {**test, "id": "independent", "local_path": "independent.dem", "map_family": "train_map", "time": 700_000, "sha256": "independent"},
        ])
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = root / "manifest.json"
            summary = root / "batch_summary.json"
            manifest.write_text(json.dumps({"demos": demos}), encoding="utf-8")
            summary.write_text(json.dumps({"demos": [
                {"demo_path": d["local_path"], "map": d["map"], "total_frames": 100_000} for d in demos
            ]}), encoding="utf-8")
            splits = build_strict_split(manifest, summary)

        self.assertEqual({d["id"] for d in splits["test"]}, PINNED_TEST_IDS)
        self.assertEqual({d["id"] for d in splits["train"]}, {"independent"})
        self.assertEqual({d["local_path"] for d in splits["val"]}, {"val.dem"})


if __name__ == "__main__":
    unittest.main()
