import argparse
import struct
import tempfile
from pathlib import Path

import numpy as np


def bake_checkpoint(path: Path) -> bytes:
    data = bytearray(path.read_bytes())
    if len(data) < 64:
        raise ValueError("Checkpoint is shorter than its header")
    magic, version, arch, seq, seq_dim, ctx_dim, hidden, trunk, steps, dims, backbone, plastic, _, _, _, _ = struct.unpack(
        "<4s15I", data[:64]
    )
    if (magic, version, seq, seq_dim, ctx_dim, hidden, trunk, steps, dims) != (
        b"SNV1", 1, 32, 22, 44, 128, 256, 16, 8
    ):
        raise ValueError("Unsupported checkpoint header")
    if arch not in (0, 1) or backbone != (129408 if arch == 0 else 129920) or plastic != 131712:
        raise ValueError("Unsupported checkpoint architecture")
    if len(data) != 64 + 4 * (backbone + 2 * plastic):
        raise ValueError("Checkpoint size does not match its header")
    if not np.isfinite(np.frombuffer(data, dtype="<f4", offset=64)).all():
        raise ValueError("Checkpoint contains non-finite weights")
    anchor = 64 + 4 * backbone
    live = anchor + 4 * plastic
    data[anchor:live] = data[live:]
    return bytes(data)


def make_header(data: bytes) -> str:
    rows = [
        "\t" + ", ".join(f"0x{value:02X}" for value in data[i:i + 16])
        for i in range(0, len(data), 16)
    ]
    return (
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace NeuralCompute\n{\n"
        f"inline constexpr size_t g_uEmbeddedWeightsSize = {len(data)}u;\n"
        "alignas(16) inline constexpr uint8_t g_aEmbeddedWeights[] = {\n"
        + ",\n".join(rows) + "\n};\n}\n"
    )


def write_header(path: Path, text: str, replace: bool) -> None:
    if replace:
        temp_path = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="ascii", newline="\n", dir=path.parent, delete=False) as out:
                temp_path = Path(out.name)
                out.write(text)
            temp_path.replace(path)
        finally:
            if temp_path is not None:
                temp_path.unlink(missing_ok=True)
    else:
        with path.open("x", encoding="ascii", newline="\n") as out:
            out.write(text)


def main() -> None:
    parser = argparse.ArgumentParser(description="Bake a live SNV1 checkpoint, using its current weights as the new anchor.")
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    data = bake_checkpoint(args.checkpoint)
    write_header(args.header, make_header(data), args.replace)
    print(f"Baked {len(data)} bytes into {args.header}")


if __name__ == "__main__":
    main()
