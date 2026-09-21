#!/usr/bin/env python3
"""Build the 64px runtime animations from the reviewed Order masters.

The painted masters and the deterministic 44/32 exports remain the source of
truth. Runtime buttons use the same full-color 64px image in four engine button
states; the state treatment is deliberately restrained so the Order subject
remains legible in the small chooser cards.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps


ORDERS = {
    "charge": "NH_charge",
    "focus-fire": "NH_focusFire",
    "riposte": "NH_riposte",
    "hold-the-line": "NH_holdTheLine",
    "brace": "NH_brace",
    "protect": "NH_protect",
    "flank": "NH_flank",
    "second-wind": "NH_secondWind",
}
STATES = ("normal", "pressed", "disabled", "highlighted")


def state_image(source: Image.Image, state: str) -> Image.Image:
    image = source.copy()
    if state == "pressed":
        shifted = Image.new("RGBA", image.size, (0, 0, 0, 0))
        shifted.alpha_composite(image, (0, 1))
        image = shifted
    elif state == "disabled":
        alpha = image.getchannel("A")
        image = ImageOps.grayscale(image).convert("RGBA")
        image.putalpha(alpha.point(lambda value: value * 0.58))
        image = ImageEnhance.Brightness(image).enhance(0.72)
    elif state == "highlighted":
        image = ImageEnhance.Brightness(image).enhance(1.14)
        image = ImageEnhance.Color(image).enhance(1.08)
    return image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)

    for slug, stem in ORDERS.items():
        source_path = args.source_root / slug / "master.png"
        with Image.open(source_path) as opened:
            source = opened.convert("RGBA")
        if source.width != source.height:
            raise SystemExit(f"{source_path}: square source required")
        source = source.resize((64, 64), Image.Resampling.LANCZOS)
        for state in STATES:
            state_image(source, state).save(args.output_root / f"{stem}_{state}.png")
        (args.output_root / f"{stem}_icon.png").write_bytes((args.output_root / f"{stem}_normal.png").read_bytes())
        descriptor = {
            "images": [
                {"group": 0, "frame": index, "file": f"{stem}_{state}.png"}
                for index, state in enumerate(STATES)
            ]
        }
        (args.output_root / f"{stem}_button.json").write_text(
            json.dumps(descriptor, indent=2) + "\n", encoding="utf-8"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
