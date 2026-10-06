#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export the authored backdrop and reviewed binary masks for Academy portraits.

The original TWCRPORT portrait pixels are deliberately never read here. At
runtime the renderer composites those external frames through the copied masks.
"""

import argparse
import hashlib
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
IMAGE_ROOT = ROOT / "Mods/new-horizons/Images"
SOURCE_BACKDROP = IMAGE_ROOT / "NH_academy/ui/crbkgtow.png"
BACKDROP_EXPORTS = (
    IMAGE_ROOT / "NH_academy_creature_portrait_backdrop.png",
    IMAGE_ROOT / "NH_academy_gremlin_icon_large.png",
    IMAGE_ROOT / "NH_academy_masterGremlin_icon_large.png",
    IMAGE_ROOT / "NH_academy_ironGolem_icon_large.png",
    IMAGE_ROOT / "NH_academy_stoneGolem_icon_large.png",
    IMAGE_ROOT / "NH_academy_mage_icon_large.png",
    IMAGE_ROOT / "NH_academy_archMage_icon_large.png",
    IMAGE_ROOT / "NH_academy_genie_icon_large.png",
    IMAGE_ROOT / "NH_academy_masterGenie_icon_large.png",
    IMAGE_ROOT / "NH_academy_naga_icon_large.png",
    IMAGE_ROOT / "NH_academy_nagaQueen_icon_large.png",
    IMAGE_ROOT / "NH_academy_giant_icon_large.png",
    IMAGE_ROOT / "NH_academy_titan_icon_large.png",
)
MASKS = {
    "gremlin": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/gremlin.png",
        IMAGE_ROOT / "NH_academy_gremlin_portrait_mask.png",
    ),
    "masterGremlin": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/masterGremlin.png",
        IMAGE_ROOT / "NH_academy_masterGremlin_portrait_mask.png",
    ),
    "ironGolem": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/ironGolem.png",
        IMAGE_ROOT / "NH_academy_ironGolem_portrait_mask.png",
    ),
    "stoneGolem": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/stoneGolem.png",
        IMAGE_ROOT / "NH_academy_stoneGolem_portrait_mask.png",
    ),
    "mage": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/mage.png",
        IMAGE_ROOT / "NH_academy_mage_portrait_mask.png",
    ),
    "archMage": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/archMage.png",
        IMAGE_ROOT / "NH_academy_archMage_portrait_mask.png",
    ),
    "genie": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/genie.png",
        IMAGE_ROOT / "NH_academy_genie_portrait_mask.png",
    ),
    "masterGenie": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/masterGenie-v2.png",
        IMAGE_ROOT / "NH_academy_masterGenie_portrait_mask.png",
    ),
    "naga": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/naga.png",
        IMAGE_ROOT / "NH_academy_naga_portrait_mask.png",
    ),
    "nagaQueen": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/nagaQueen-v2.png",
        IMAGE_ROOT / "NH_academy_nagaQueen_portrait_mask.png",
    ),
    "giant": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/giant.png",
        IMAGE_ROOT / "NH_academy_giant_portrait_mask.png",
    ),
    "titan": (
        ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/titan-v3.png",
        IMAGE_ROOT / "NH_academy_titan_portrait_mask.png",
    ),
}
MASTER_GENIE_V2_SHA256 = "9f5291e7d50b29a4a16d8c54aeba2de9b4566456d5b5823e4d18023ac6659a40"
NAGA_SHA256 = "c4e58bcf3c9b84f63ec7137c37da23fe3bc898a0e77e653ee6ad016c3997e453"
NAGA_QUEEN_V2_SHA256 = "aa282c71f59b720ccc2604e81b3bc1527b8e58b7ab60403eee9fb28eb9f64c30"
GIANT_SHA256 = "2e576469ff77e6c07ad529caedead80bc342e39f7fd5c87bd293dd53a83102bd"
TITAN_V3_SHA256 = "df23c30a56d0f08f7b24e16d05d6de2e55acedfa75bcbfe3143577f5fc3b8092"
# Pillow's right/bottom-excluded crop rectangle from the approved composition.
SOURCE_CROP = (0, 10, 100, 120)
OUTPUT_SIZE = (58, 64)


def render_backdrop() -> Image.Image:
    with Image.open(SOURCE_BACKDROP) as source:
        if source.mode != "RGB" or source.size != (100, 130):
            raise ValueError(f"expected authored 100x130 RGB backdrop: {source.mode} {source.size}")
        return source.crop(SOURCE_CROP).resize(OUTPUT_SIZE, Image.Resampling.LANCZOS)


def verify(check: bool, parser: argparse.ArgumentParser) -> None:
    if not SOURCE_BACKDROP.is_file():
        parser.error(f"missing authored backdrop source: {SOURCE_BACKDROP}")
    master_genie_matte = MASKS["masterGenie"][0]
    if not master_genie_matte.is_file():
        parser.error(f"missing pinned Master Genie v2 matte: {master_genie_matte}")
    master_genie_sha256 = hashlib.sha256(master_genie_matte.read_bytes()).hexdigest()
    if master_genie_sha256 != MASTER_GENIE_V2_SHA256:
        parser.error(f"Master Genie v2 matte SHA256 differs from the reviewed source: {master_genie_matte}")
    naga_matte = MASKS["naga"][0]
    if not naga_matte.is_file():
        parser.error(f"missing pinned Naga matte: {naga_matte}")
    naga_sha256 = hashlib.sha256(naga_matte.read_bytes()).hexdigest()
    if naga_sha256 != NAGA_SHA256:
        parser.error(f"Naga matte SHA256 differs from the reviewed source: {naga_matte}")
    naga_queen_matte = MASKS["nagaQueen"][0]
    if not naga_queen_matte.is_file():
        parser.error(f"missing pinned Naga Queen v2 matte: {naga_queen_matte}")
    naga_queen_sha256 = hashlib.sha256(naga_queen_matte.read_bytes()).hexdigest()
    if naga_queen_sha256 != NAGA_QUEEN_V2_SHA256:
        parser.error(f"Naga Queen v2 matte SHA256 differs from the reviewed source: {naga_queen_matte}")
    giant_matte = MASKS["giant"][0]
    if not giant_matte.is_file():
        parser.error(f"missing pinned Giant matte: {giant_matte}")
    giant_sha256 = hashlib.sha256(giant_matte.read_bytes()).hexdigest()
    if giant_sha256 != GIANT_SHA256:
        parser.error(f"Giant matte SHA256 differs from the reviewed source: {giant_matte}")
    titan_matte = MASKS["titan"][0]
    if not titan_matte.is_file():
        parser.error(f"missing pinned Titan v3 matte: {titan_matte}")
    titan_sha256 = hashlib.sha256(titan_matte.read_bytes()).hexdigest()
    if titan_sha256 != TITAN_V3_SHA256:
        parser.error(f"Titan v3 matte SHA256 differs from the reviewed source: {titan_matte}")
    expected_backdrop = render_backdrop()
    expected_pixels = expected_backdrop.tobytes()

    if check:
        for path in BACKDROP_EXPORTS:
            if not path.is_file():
                parser.error(f"missing generated backdrop/fallback export: {path}")
            with Image.open(path) as actual:
                if actual.mode != "RGB" or actual.size != OUTPUT_SIZE or actual.tobytes() != expected_pixels:
                    parser.error(f"backdrop/fallback export differs from authored crop: {path}")
        for creature, (source, output) in MASKS.items():
            if not source.is_file() or not output.is_file():
                parser.error(f"missing {creature} approved matte or runtime copy: {source} / {output}")
            if source.read_bytes() != output.read_bytes():
                parser.error(f"{creature} runtime mask is not a byte-identical copy of its approved matte")
            with Image.open(source) as matte:
                if matte.mode != "L" or matte.size != OUTPUT_SIZE:
                    parser.error(f"{creature} approved matte must be grayscale {OUTPUT_SIZE}: {matte.mode} {matte.size}")
                values = set(matte.tobytes())
                if values - {0, 255}:
                    parser.error(f"{creature} approved matte must contain only black and white pixels")
        print("PASS: Academy portrait backdrops and runtime masks match their authored sources")
        return

    for path in BACKDROP_EXPORTS:
        expected_backdrop.save(path, format="PNG")
    for source, output in MASKS.values():
        if not source.is_file():
            parser.error(f"missing approved matte: {source}")
        with Image.open(source) as matte:
            if matte.mode != "L" or matte.size != OUTPUT_SIZE or set(matte.tobytes()) - {0, 255}:
                parser.error(f"approved matte must be binary grayscale {OUTPUT_SIZE}: {source}")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(source.read_bytes())
    print(f"Wrote authored backdrop fallbacks and {len(MASKS)} byte-identical reviewed matte copies")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify exports without writing")
    args = parser.parse_args()
    verify(args.check, parser)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
