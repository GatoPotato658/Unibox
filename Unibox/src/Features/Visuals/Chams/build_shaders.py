import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def find_fxc():
    configured = os.environ.get("FXC")
    if configured:
        return configured
    executable = shutil.which("fxc")
    if executable:
        return executable
    root = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
    candidates = sorted((root / "Windows Kits/10/bin").glob("*/x64/fxc.exe"))
    if candidates:
        return str(candidates[-1])
    raise SystemExit("fxc was not found; pass --fxc or set FXC")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fxc")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    compiler = args.fxc or find_fxc()
    directory = Path(__file__).resolve().parent
    effects = [
        "Flat", "Shaded", "Metallic", "Pearlescent", "Glass", "Holographic",
        "Iridescent", "Chrome", "Gradient", "Wireframe", "Crystal", "Emissive",
        "Plasma", "Nebula", "Carbon", "Brushed", "Hex", "Digital",
        "Lava", "Bubble", "Neon", "Network", "Soap", "Aurora", "Aquarium",
        "Daydream", "Fresnel", "Galaxy", "Matrix", "Fire", "Frost", "Toon",
        "Cells", "Circuit", "Electric", "Spectrum", "Marble", "Thermal", "Camo",
    ]
    shaders = [
        ("Rigid", "vs_3_0", "Rigid", "VERTEX_SHADER=1"),
        ("Skinned", "vs_3_0", "Skinned", "VERTEX_SHADER=1"),
    ]
    shaders += [(name, "ps_3_0", "Material", f"EFFECT={i}")
                for i, name in enumerate(effects, 1)]
    lines = ["#pragma once", "", "#include <cstdint>", "", "namespace D3DMaterialShaders", "{"]
    with tempfile.TemporaryDirectory() as temporary:
        for name, target, entry, define in shaders:
            output = Path(temporary) / f"{name}.cso"
            subprocess.run(
                [compiler, "/nologo", "/O3", "/Ges", "/WX", "/T", target,
                 "/E", entry, "/D", define, "/Fo", str(output),
                 str(directory / "D3DMaterials.hlsl")],
                check=True,
            )
            bytecode = output.read_bytes()
            words = struct.unpack(f"<{len(bytecode) // 4}I", bytecode)
            lines += [f"\tinline constexpr std::uint32_t {name}[] =", "\t{"]
            for offset in range(0, len(words), 8):
                lines.append("\t\t" + ", ".join(f"0x{word:08x}" for word in words[offset:offset + 8]) + ",")
            lines += ["\t};", ""]
            print(f"{name}: {len(bytecode)} bytes")
    lines += ["}", ""]
    contents = "\n".join(lines)
    output_header = directory / "D3DMaterialShaders.h"
    if args.check:
        if not output_header.exists() or output_header.read_text(encoding="utf-8") != contents:
            raise SystemExit("D3DMaterialShaders.h is not up to date")
    else:
        output_header.write_text(contents, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
