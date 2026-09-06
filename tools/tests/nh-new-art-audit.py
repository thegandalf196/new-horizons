#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline first-slice art checks. Reads no purchaser assets; never launches a GUI.

Requires Pillow (also required by the artwork generator). Optional regeneration
runs a reviewed copy of that generator in temporary storage, never over product
outputs. This checks reproducibility/geometry, not artistic rights or game rendering.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SCHOOLS = ("light", "nature", "sorcery", "havoc", "shadow", "chaos")
BUTTONS = ("charge", "holdTheLine", "advance", "aggressive", "defensive", "spells", "cancel")
HERO_GLYPHS = ("attack", "defense", "power", "knowledge", "mana", "leadership", "movement",
               "morale", "luck", "siege", "mastery", "core", "elite", "champion", "growth")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(reproduce):
    source = ROOT / "assets/new-horizons/svg"
    images = ROOT / "Mods/new-horizons/Images"
    svgs = sorted(source.glob("*.svg"))
    pngs = sorted(images.glob("*.png"))
    animations = sorted(images.glob("*.json"))
    require(bool(svgs) and bool(animations), "Missing artwork")
    require({p.stem for p in svgs} == {p.stem for p in pngs}, "SVG/PNG inventory mismatch")
    for path in svgs:
        tree = ET.parse(path).getroot()
        require(all(element.tag.split("}")[-1] in {"svg", "polygon", "polyline", "circle"}
                    for element in tree.iter()), f"Unexpected non-geometric SVG element: {path.name}")
        require(all(not key.lower().endswith("href") and "url(" not in value.lower()
                    for element in tree.iter() for key, value in element.attrib.items()),
                f"External/embedded SVG reference: {path.name}")
        with Image.open(images / (path.stem + ".png")) as image:
            require(image.mode == "RGBA", f"Not RGBA: {path.name}")
            require(image.size == (int(tree.attrib["width"]), int(tree.attrib["height"])),
                    f"SVG/PNG size mismatch: {path.name}")
    for path in animations:
        frames = json.loads(path.read_text())["images"]
        bookmark = path.stem.endswith("_bookmark")
        require(len(frames) == (2 if bookmark else 4), f"Wrong state count: {path.name}")
        expected_size = (80, 60) if bookmark else (48, 36) if path.stem == "NH_hero_actions_entry" else (64, 64)
        states = ("selected", "unselected") if bookmark else ("normal", "pressed", "disabled", "highlighted")
        prefix = path.stem.removesuffix("_button")
        for index, frame in enumerate(frames):
            name = frame["file"]
            require(name == f"{prefix}_{states[index]}.png", f"Wrong semantic state order: {path.name}")
            require(Path(name).name == name and "/" not in name and "\\" not in name,
                    f"Non-local image reference: {name}")
            require(frame["frame"] == index and frame["group"] == 0, f"Non-contiguous frames: {path.name}")
            with Image.open(images / name) as image:
                require(image.size == expected_size, f"Wrong state dimensions: {name}")
    for name in BUTTONS:
        require((images / f"NH_{name}_button.json").is_file(), f"Missing command control: {name}")
    require((images / "NH_hero_actions_entry.json").is_file(), "Missing entry animation")
    with Image.open(images / "NH_hero_actions_back.png") as image:
        require(image.size == (640, 520), "Wrong chooser background size")
    for school in SCHOOLS:
        require((images / f"NH_{school}_bookmark.json").is_file(), f"Missing bookmark: {school}")
        with Image.open(images / f"NH_{school}_header.png") as image:
            require(image.size == (160, 96), f"Wrong school header size: {school}")
            require(image.getchannel("A").crop((0, 0, 160, 28)).getextrema() == (0, 0),
                    f"Nontransparent top28 header rows: {school}")
    for glyph in HERO_GLYPHS:
        for size in (32, 64):
            with Image.open(images / f"NH_hero_{glyph}_{size}.png") as image:
                require(image.size == (size, size), f"Wrong hero display glyph size: {glyph}/{size}")
    files = svgs + pngs + animations
    hashes = {str(path.relative_to(ROOT)): digest(path) for path in files}
    if reproduce:
        # The reviewed generator derives output roots from __file__, not cwd.
        with tempfile.TemporaryDirectory(prefix="nh-art-audit-") as temporary:
            sandbox = Path(temporary)
            copied = sandbox / "assets/new-horizons/generate_icons.py"
            copied.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / "assets/new-horizons/generate_icons.py", copied)
            subprocess.run([sys.executable, str(copied)], cwd=sandbox, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            for directory in ("assets/new-horizons/svg", "Mods/new-horizons/Images"):
                expected = {p.name for p in (ROOT / directory).iterdir() if p.is_file()}
                actual = {p.name for p in (sandbox / directory).iterdir() if p.is_file()}
                require(expected == actual, f"Regenerated inventory mismatch: {directory}")
            for name, value in hashes.items():
                require(digest(sandbox / name) == value, f"Regenerated bytes differ: {name}")
    require(all(digest(ROOT / name) == value for name, value in hashes.items()),
            "Source outputs changed during audit")
    return {"svg": len(svgs), "png": len(pngs), "animation_json": len(animations),
            "reproduced": reproduce, "hashes": hashes,
            "scope": "Offline geometry/dimensions/state/padding checks; not gameplay or rights clearance"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reproduce", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = audit(args.reproduce)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {result['svg']} SVG, {result['png']} PNG, {result['animation_json']} animations; "
          f"regeneration={result['reproduced']}")


if __name__ == "__main__":
    main()
