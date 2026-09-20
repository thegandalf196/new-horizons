#!/usr/bin/env python3
"""Export the generated New Horizons secondary-skill masters.

The masters are high-resolution, square RGB PNGs.  The game consumes four
legacy skill slots, whose aspect ratios are not all square, so this exporter
performs the same explicit LANCZOS reduction for every slot and adds only the
small rank markers that identify Basic/Advanced/Expert.  It never touches the
approved six-school families or any other image in the module.

SPDX-License-Identifier: CC0-1.0
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageOps


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent
IMAGES = ROOT / "Mods/new-horizons/Images"
SIZES = {
    "small": (32, 32),
    "medium": (44, 44),
    "large": (82, 93),
    "scenarioBonus": (58, 64),
}
RANKS = ("basic", "advanced", "expert")

# One generated master per genuinely new/provisional skill.  Classic skills
# use the canonical VCMI SECSK* families in newHorizonsSkills.json instead.
MASTERS = (
    "battlecraft",
    "recruitment",
    "warcasting",
    "divineMandate",
    "sylvanLuck",
    "metamagic",
    "shroudOfMalassa",
    "demonicGating",
    "bloodrage",
    "bulwarkOfTheMire",
    "elementalRebirth",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rank_image(master: Image.Image, rank: int) -> Image.Image:
    """Create one controlled, readable mastery variant from a master."""

    # Keep rank changes material and restrained: the subject remains the same
    # painting, while expert receives a little more contrast and saturation.
    factors = ((0.94, 0.94, 0.98), (1.02, 1.06, 1.08), (1.09, 1.11, 1.16))[rank]
    image = ImageEnhance.Brightness(master).enhance(factors[0])
    image = ImageEnhance.Contrast(image).enhance(factors[1])
    image = ImageEnhance.Color(image).enhance(factors[2])
    image = image.convert("RGBA")

    # Three subdued, gold-lit rank pips mirror the approved school-art
    # convention without drawing a border or obscuring the still life.
    draw = ImageDraw.Draw(image, "RGBA")
    width, height = image.size
    radius = max(1.0, min(width, height) / 24.0)
    y = height - max(3.0, min(width, height) * 0.075)
    gap = max(2.0, min(width, height) / 5.8)
    center = width / 2.0
    for marker in range(3):
        x = center + (marker - 1) * gap
        box = (x - radius, y - radius, x + radius, y + radius)
        fill = (242, 216, 117, 245) if marker < rank + 1 else (48, 42, 37, 235)
        outline = (242, 216, 117, 245) if marker < rank + 1 else (117, 96, 64, 230)
        draw.ellipse(box, fill=fill, outline=outline, width=max(1, round(radius / 2)))
    return image


def render(master_path: Path, output: Path, rank: int, size: tuple[int, int]) -> None:
    with Image.open(master_path) as opened:
        image = ImageOps.exif_transpose(opened).convert("RGB")
    if image.width != image.height:
        raise ValueError(f"master is not square: {master_path}")
    variant = rank_image(image, rank)
    resized = variant.resize(size, Image.Resampling.LANCZOS)
    output.parent.mkdir(parents=True, exist_ok=True)
    resized.save(output, format="PNG", optimize=False, compress_level=9)


def expected_files() -> list[Path]:
    return [
        IMAGES / f"NH_{skill}_{rank}_{size}.png"
        for skill in MASTERS
        for rank in RANKS
        for size in SIZES
    ]


def export() -> dict:
    manifest: dict = {
        "schema_version": 1,
        "license": "CC0-1.0",
        "method": "Pillow LANCZOS reduction of generated square masters; rank color/material adjustment plus three rank pips",
        "sizes": {key: list(value) for key, value in SIZES.items()},
        "skills": {},
    }
    for skill in MASTERS:
        master_path = SOURCE / f"NH_{skill}_master.png"
        if not master_path.is_file():
            raise FileNotFoundError(master_path)
        with Image.open(master_path) as image:
            if image.width != image.height:
                raise ValueError(f"master is not square: {master_path}")
            master_dimensions = list(image.size)
        skill_manifest = {
            "master": master_path.name,
            "master_sha256": sha256(master_path),
            "master_dimensions": master_dimensions,
            "outputs": {},
        }
        for rank_index, rank in enumerate(RANKS):
            skill_manifest["outputs"][rank] = {}
            for size_name, size in SIZES.items():
                target = IMAGES / f"NH_{skill}_{rank}_{size_name}.png"
                render(master_path, target, rank_index, size)
                skill_manifest["outputs"][rank][size_name] = {
                    "file": target.name,
                    "dimensions": list(size),
                    "sha256": sha256(target),
                }
        manifest["skills"][skill] = skill_manifest
    manifest_path = SOURCE / "NH_skill_art_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def check() -> dict:
    manifest_path = SOURCE / "NH_skill_art_manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    missing = []
    wrong = []
    for skill in MASTERS:
        master = SOURCE / f"NH_{skill}_master.png"
        data = manifest["skills"][skill]
        if sha256(master) != data["master_sha256"]:
            wrong.append(str(master))
        for rank in RANKS:
            for size_name, size in SIZES.items():
                target = IMAGES / f"NH_{skill}_{rank}_{size_name}.png"
                if not target.is_file():
                    missing.append(str(target))
                    continue
                with Image.open(target) as image:
                    if image.size != size or image.mode != "RGBA":
                        wrong.append(str(target))
                if sha256(target) != data["outputs"][rank][size_name]["sha256"]:
                    wrong.append(str(target))
    if missing or wrong:
        raise ValueError(json.dumps({"missing": missing, "wrong": wrong}, indent=2))
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify files and manifest without rewriting")
    args = parser.parse_args()
    result = check() if args.check else export()
    print(f"PASS: {len(result['skills'])} provisional skill masters; {len(expected_files())} RGBA exports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
