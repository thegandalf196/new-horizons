#!/usr/bin/env python3
"""Mechanically crop and export generated Sylvan Luck perk artwork."""

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
    outputs = {}
    review = Image.new("RGB", (3 * 180, 190), "#211b18")

    with Image.open(here / generation["atlas"]) as source:
        assert list(source.size) == generation["dimensions"]
        for index, cell in enumerate(generation["cells"]):
            key = cell["key"]
            master = here / f"{key}.png"
            source.crop(cell["box"]).save(master)
            exported = here / "exports" / key
            exported.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([
                sys.executable, str(args.export_helper), "--input", str(master),
                "--output-dir", str(exported), "--name", key,
                "--sizes", "44", "--fit", "contain", "--padding-color", "#241c18",
            ], check=True, stdout=subprocess.DEVNULL)
            normal = Image.open(exported / f"{key}-44.png").convert("RGBA")
            states = {
                "normal": normal,
                "pressed": ImageEnhance.Brightness(normal).enhance(0.82),
                "disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
                "highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
            }
            frames = []
            for frame, (state, image) in enumerate(states.items()):
                filename = f"{key}_{state}.png"
                path = live / filename
                if path.exists():
                    raise FileExistsError(path)
                image.save(path)
                outputs[filename] = hashlib.sha256(path.read_bytes()).hexdigest()
                frames.append({"group": 0, "frame": frame, "file": filename})
            descriptor = live / f"{key}.json"
            if descriptor.exists():
                raise FileExistsError(descriptor)
            descriptor.write_text(json.dumps({"images": frames}, indent=2) + "\n")
            outputs[descriptor.name] = hashlib.sha256(descriptor.read_bytes()).hexdigest()
            review.paste(normal.convert("RGB"), (index * 180, 0))
            review.paste(normal.resize((132, 132), Image.Resampling.NEAREST).convert("RGB"), (index * 180, 50))

    review.save(here / "native-review.png")
    (here / "runtime-manifest.json").write_text(json.dumps({
        "method": "Generated atlas crop, contain/LANCZOS export, brightness/color state transforms",
        "files": outputs,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
