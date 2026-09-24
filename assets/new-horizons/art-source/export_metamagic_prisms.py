#!/usr/bin/env python3
"""Assemble HoMM3-skill square exports into native Metamagic slots.

This does not generate, resize, recolor or repaint art. Supply the three versioned
directories produced by the HoMM3 skill's export_art.py with sizes 32,44,82,58.
The two non-square slots receive centered, transparent padding only.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent / "metamagic-prisms-v2"
IMAGES = ROOT / "Mods/new-horizons/Images"
SLOTS = {"small": (32, 32), "medium": (44, 44),
         "large": (82, 93), "scenarioBonus": (58, 64)}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sanitized_master(data):
    """Keep exact PNG image chunks, without generated metadata carriers."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected a PNG master")
    result = data[:8]
    offset = 8
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        chunk = data[offset:offset + length + 12]
        if kind not in (b"caBX", b"IHDR", b"IDAT", b"IEND"):
            raise ValueError(f"Unexpected PNG chunk: {kind!r}")
        if kind != b"caBX":
            result += chunk
        offset += length + 12
    return result


def fit_export(square, dimensions):
    """Preserve square pixels and alpha, centering without a mask or resize."""
    if square.size != (dimensions[0], dimensions[0]):
        raise ValueError("Input must be the skill helper's native square export")
    canvas = Image.new("RGBA", dimensions, (0, 0, 0, 0))
    canvas.paste(square.convert("RGBA"), (0, (dimensions[1] - dimensions[0]) // 2))
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for rank in ("basic", "advanced", "expert"):
        parser.add_argument(f"--{rank}-exports", type=Path, required=True)
    args = parser.parse_args()
    if SOURCE.exists():
        raise FileExistsError(f"Refusing to replace retained source directory: {SOURCE}")
    SOURCE.mkdir()
    manifest = {"status": "provisional; rendered acceptance pending",
                "method": "HoMM3 Art generation/edit; skill export_art.py LANCZOS squares; centered transparent padding, no stretching",
                "ranks": {}}
    for rank in ("basic", "advanced", "expert"):
        exports = getattr(args, f"{rank}_exports")
        original = exports / "master.png"
        master = SOURCE / f"{rank}-master.png"
        master.write_bytes(sanitized_master(original.read_bytes()))
        with Image.open(master) as image:
            dimensions = list(image.size)
        record = {"master": master.name, "sha256": digest(master),
                  "original_sha256": digest(original), "dimensions": dimensions,
                  "outputs": {}}
        for slot, dimensions in SLOTS.items():
            square_path = exports / f"metamagic-{rank}-{dimensions[0]}.png"
            with Image.open(square_path) as square:
                canvas = fit_export(square, dimensions)
            target = IMAGES / f"NH_metamagic_prism_{rank}_{slot}.png"
            if target.exists():
                raise FileExistsError(target)
            canvas.save(target, compress_level=9)
            record["outputs"][slot] = {"file": target.name, "sha256": digest(target),
                "dimensions": list(dimensions), "square_sha256": digest(square_path),
                "offset": [0, (dimensions[1] - dimensions[0]) // 2]}
        manifest["ranks"][rank] = record
    (SOURCE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
