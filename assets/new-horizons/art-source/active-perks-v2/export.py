#!/usr/bin/env python3
"""Mechanically export the three active-perk paintings and runtime states.

The paintings themselves are never generated or modified by this script.  It
uses the homm3-art export helper for square masters, then derives the four
44x44 runtime states from the normal reduction.  Existing source, export, or
runtime files are never overwritten.

Run with an absolute Pillow-enabled interpreter and an absolute
``--export-helper`` path, for example::

    /usr/bin/python3 export.py --export-helper /path/to/homm3-art/scripts/export_art.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from PIL import Image, ImageEnhance


ASSETS = (
    ("inspirational-leader", "NH_perk_inspirational_leader"),
    ("wild-chance", "NH_perk_wild_chance"),
    ("perfect-moment", "NH_perk_perfect_moment"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_export(destination: Path, slug: str) -> None:
    """Validate an existing helper export so reruns remain non-destructive."""
    required = (
        destination / "master.png",
        destination / f"{slug}-44.png",
        destination / f"{slug}-32.png",
        destination / f"{slug}-comparison.png",
        destination / f"{slug}-manifest.json",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("Existing export is incomplete: " + ", ".join(missing))


def make_export(helper: Path, here: Path, slug: str) -> Path:
    source = here / f"{slug}-master.png"
    destination = here / "exports" / slug
    if destination.exists():
        require_export(destination, slug)
        return destination
    destination.parent.mkdir(exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            str(helper),
            "--input",
            str(source),
            "--output-dir",
            str(destination),
            "--name",
            slug,
            "--sizes",
            "44,32",
        ],
        check=True,
    )
    return destination


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--export-helper", required=True, type=Path)
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    root = here.parents[3]
    live = root / "Mods/new-horizons/Images"
    helper = args.export_helper.resolve(strict=True)
    live.mkdir(parents=True, exist_ok=True)
    outputs: dict[str, str] = {}
    review = Image.new("RGB", (len(ASSETS) * 180, 190), "#211b18")

    for index, (slug, stem) in enumerate(ASSETS):
        destination = make_export(helper, here, slug)
        normal = Image.open(destination / f"{slug}-44.png").convert("RGBA")
        states = {
            "normal": normal,
            "pressed": ImageEnhance.Brightness(normal).enhance(0.82),
            "disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
            "highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
        }
        frames = []
        for frame, (state, image) in enumerate(states.items()):
            filename = f"{stem}_{state}.png"
            path = live / filename
            if path.exists():
                raise FileExistsError(path)
            image.save(path, format="PNG")
            outputs[filename] = sha256(path)
            frames.append({"group": 0, "frame": frame, "file": filename})

        descriptor = live / f"{stem}.json"
        if descriptor.exists():
            raise FileExistsError(descriptor)
        descriptor.write_text(json.dumps({"images": frames}, indent=2) + "\n", encoding="utf-8")
        outputs[descriptor.name] = sha256(descriptor)

        small = Image.open(destination / f"{slug}-32.png").convert("RGBA")
        x = index * 180
        review.paste(normal.convert("RGB"), (x, 0))
        review.paste(small.convert("RGB"), (x + 50, 0))
        review.paste(normal.resize((132, 132), Image.Resampling.NEAREST).convert("RGB"), (x, 50))
        review.paste(small.resize((96, 96), Image.Resampling.NEAREST).convert("RGB"), (x + 50, 50))

    review_path = here / "native-review.png"
    if review_path.exists():
        raise FileExistsError(review_path)
    review.save(review_path, format="PNG")
    outputs[review_path.name] = sha256(review_path)

    manifest = {
        "method": "homm3-art export_art.py square reduction; brightness/color-only runtime states",
        "status": "provisional; not final user-approved artwork",
        "assets": [stem for _, stem in ASSETS],
        "files": outputs,
    }
    manifest_path = here / "runtime-manifest.json"
    if manifest_path.exists():
        raise FileExistsError(manifest_path)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
