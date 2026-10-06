#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export the authored backdrop and reviewed binary masks for Academy portraits.

The original TWCRPORT portrait pixels are deliberately never read here. At
runtime the renderer composites those external frames through the copied masks.
"""

import argparse
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
}
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
