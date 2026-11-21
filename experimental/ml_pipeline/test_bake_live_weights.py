import re
import struct
import tempfile
import unittest
from pathlib import Path

from bake_live_weights import bake_checkpoint, make_header, write_header


class BakeTest(unittest.TestCase):
    def test_existing_header_requires_replace(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "weights.h"
            path.write_text("old", encoding="ascii")
            with self.assertRaises(FileExistsError):
                write_header(path, "new", False)
            self.assertEqual(path.read_text(encoding="ascii"), "old")
            write_header(path, "new", True)
            self.assertEqual(path.read_text(encoding="ascii"), "new")

    def test_rebases_live_weights_and_preserves_model(self):
        backbone, plastic = 129408, 131712
        header = struct.pack(
            "<4s15I", b"SNV1", 1, 0, 32, 22, 44, 128, 256, 16, 8, backbone, plastic, 0, 0, 0, 0
        )
        data = header + struct.pack("<f", 0.25) * backbone + struct.pack("<f", 0.5) * plastic + struct.pack("<f", 0.75) * plastic
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "weights.bin"
            path.write_bytes(data)
            baked = bake_checkpoint(path)
            path.write_bytes(data[:-1])
            with self.assertRaises(ValueError):
                bake_checkpoint(path)

        start = 64 + 4 * backbone
        self.assertEqual(baked[:start], data[:start])
        self.assertEqual(baked[start:start + 4 * plastic], data[start + 4 * plastic:])
        self.assertEqual(baked[start + 4 * plastic:], data[start + 4 * plastic:])
        decoded = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", make_header(baked)))
        self.assertEqual(decoded, baked)


if __name__ == "__main__":
    unittest.main()
