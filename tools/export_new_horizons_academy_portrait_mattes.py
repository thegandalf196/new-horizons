#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Mechanically export the approved Academy Gremlin portrait matte.

This reproduces the existing geometry-only 58x64 mask from its generated
high-resolution master. It does not read or write purchaser-original artwork.
"""

import argparse
import hashlib
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/masters/gremlin-matte.png"
MASK = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/gremlin.png"
SIZE = (58, 64)
THRESHOLD = 128


def render_matte() -> Image.Image:
    """Return the pinned LANCZOS reduction and binary threshold result."""
    with Image.open(MASTER) as source:
        reduced = source.convert("L").resize(SIZE, Image.Resampling.LANCZOS)
    return reduced.point(lambda value: 255 if value >= THRESHOLD else 0, mode="L")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in native mask without writing it",
    )
    args = parser.parse_args()

    expected = render_matte()
    if args.check:
        if not MASK.is_file():
            parser.error(f"missing approved native mask: {MASK}")
        with Image.open(MASK) as current:
            if current.mode != "L" or current.size != SIZE:
                parser.error(f"native mask must be grayscale {SIZE[0]}x{SIZE[1]}: {current.mode} {current.size}")
            actual_pixels = current.tobytes()
        if actual_pixels != expected.tobytes():
            parser.error("approved native mask differs from the mechanical master reduction")
        digest = hashlib.sha256(MASK.read_bytes()).hexdigest()
        print(f"PASS: approved Gremlin matte matches LANCZOS {SIZE[0]}x{SIZE[1]} threshold >= {THRESHOLD}; sha256={digest}")
        return 0

    MASK.parent.mkdir(parents=True, exist_ok=True)
    expected.save(MASK, format="PNG")
    digest = hashlib.sha256(MASK.read_bytes()).hexdigest()
    print(f"Wrote {MASK.relative_to(ROOT)} ({SIZE[0]}x{SIZE[1]} L); sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
