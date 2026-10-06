#!/usr/bin/env python3
"""Mechanical Cabir draft reduction; never installs runtime creature art."""

from pathlib import Path
import argparse

from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--height", type=int, default=60)
    args = parser.parse_args()
    if args.height < 1:
        parser.error("height must be positive")
    with Image.open(args.input) as source:
        master = source.convert("RGBA")
    bounds = master.getchannel("A").getbbox()
    if bounds is None:
        parser.error("input has no visible creature")
    creature = master.crop(bounds)
    width = max(1, round(creature.width * args.height / creature.height))
    native = creature.resize((width, args.height), Image.Resampling.LANCZOS)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    native.save(args.output_dir / "standing-preview.png")
    native.resize((width * 4, args.height * 4), Image.Resampling.NEAREST).save(
        args.output_dir / "standing-preview-4x.png"
    )
    print(f"Master {master.size}; alpha bounds {bounds}; proposed body size {native.size}")


if __name__ == "__main__":
    main()
