#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Mechanical export of built-in generated atlas cells, never creative drawing.

Run with an absolute Pillow-enabled interpreter and --export-helper pointing at
the homm3-art skill's scripts/export_art.py. New directories/files only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageEnhance


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--export-helper", required=True, type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[3]
    live = root / "Mods/new-horizons/Images"
    generation = json.loads((here / "generation.json").read_text())
    masters = here / "cells"
    exports = here / "exports"
    masters.mkdir(exist_ok=False)
    exports.mkdir(exist_ok=False)
    outputs = {}
    review = Image.new("RGB", (6 * 180, 4 * 190), "#211b18")
    for group, atlas in enumerate(generation["atlases"]):
        with Image.open(here / atlas["file"]) as source:
            assert list(source.size) == atlas["dimensions"]
            for index, cell in enumerate(atlas["cells"]):
                key = cell["key"]
                master = masters / (key + ".png")
                source.crop(cell["box"]).save(master)
                destination = exports / key
                subprocess.run([sys.executable, str(args.export_helper), "--input", str(master),
                    "--output-dir", str(destination), "--name", key, "--sizes", "44,32",
                    "--fit", "contain", "--padding-color", "#241c18"], check=True, stdout=subprocess.DEVNULL)
                normal = Image.open(destination / (key + "-44.png")).convert("RGBA")
                states = {"normal": normal,
                    "pressed": ImageEnhance.Brightness(normal).enhance(0.82),
                    "disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
                    "highlighted": ImageEnhance.Brightness(normal).enhance(1.13)}
                frames = []
                for frame, (state, image) in enumerate(states.items()):
                    filename = key + "_" + state + ".png"
                    path = live / filename
                    assert not path.exists(), path
                    image.save(path)
                    outputs[filename] = hashlib.sha256(path.read_bytes()).hexdigest()
                    frames.append({"group": 0, "frame": frame, "file": filename})
                descriptor = live / (key + ".json")
                assert not descriptor.exists(), descriptor
                descriptor.write_text(json.dumps({"images": frames}, indent=2) + "\n")
                outputs[descriptor.name] = hashlib.sha256(descriptor.read_bytes()).hexdigest()
                small = Image.open(destination / (key + "-32.png")).convert("RGBA")
                if key.startswith("NH_capability_"):
                    path = live / (key + "_32.png")
                    assert not path.exists(), path
                    small.save(path)
                    outputs[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
                    wrapper = live / (key + "_32.json")
                    assert not wrapper.exists(), wrapper
                    wrapper.write_text(json.dumps({"images": [{"group": 0, "frame": 0, "file": path.name}]}, indent=2) + "\n")
                    outputs[wrapper.name] = hashlib.sha256(wrapper.read_bytes()).hexdigest()
                x, y = index * 180, group * 190
                review.paste(normal.convert("RGB"), (x, y))
                review.paste(small.convert("RGB"), (x + 50, y))
                review.paste(normal.resize((132, 132), Image.Resampling.NEAREST).convert("RGB"), (x, y + 50))
    review.save(here / "native-review.png")
    (here / "runtime-manifest.json").write_text(json.dumps({
        "method": "Atlas crop then explicit contain padding and skill helper LANCZOS; brightness/color only for states",
        "files": outputs}, indent=2) + "\n")


if __name__ == "__main__":
    main()
