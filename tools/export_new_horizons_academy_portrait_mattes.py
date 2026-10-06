#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Mechanically export Academy portrait mattes from generated geometry masters.

Use ``--creature`` to export a specific named master, or ``--all`` for all
currently available named masters. This tool only performs the pinned
mechanical reduction; it does not approve a mask and does not read or write
purchaser-original artwork.
"""

import argparse
import hashlib
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
REVISION = ROOT / "assets/new-horizons/academy/portrait-revisions/v1"
SIZE = (58, 64)
THRESHOLD = 128
CREATURES = {
    "gremlin": {
        "master": REVISION / "masters/gremlin-matte.png",
        "mask": REVISION / "mattes/gremlin.png",
    },
    "masterGremlin": {
        "master": REVISION / "masters/masterGremlin-matte.png",
        "mask": REVISION / "mattes/masterGremlin.png",
    },
    "stoneGargoyle": {
        "master": REVISION / "masters/stoneGargoyle-matte.png",
        "mask": REVISION / "mattes/stoneGargoyle.png",
    },
    "obsidianGargoyle": {
        "master": REVISION / "masters/obsidianGargoyle-matte.png",
        "mask": REVISION / "mattes/obsidianGargoyle.png",
    },
    "obsidianGargoyleV2": {
        "master": REVISION / "masters/obsidianGargoyle-matte-v2.png",
        "mask": REVISION / "mattes/obsidianGargoyle-v2.png",
    },
    "obsidianGargoyleV3": {
        "master": REVISION / "masters/obsidianGargoyle-matte-v3.png",
        "mask": REVISION / "mattes/obsidianGargoyle-v3.png",
    },
    "ironGolem": {
        "master": REVISION / "masters/ironGolem-matte.png",
        "mask": REVISION / "mattes/ironGolem.png",
    },
    "stoneGolem": {
        "master": REVISION / "masters/stoneGolem-matte.png",
        "mask": REVISION / "mattes/stoneGolem.png",
    },
    "mage": {
        "master": REVISION / "masters/mage-matte.png",
        "mask": REVISION / "mattes/mage.png",
    },
    "archMage": {
        "master": REVISION / "masters/archMage-matte.png",
        "mask": REVISION / "mattes/archMage.png",
    },
    "genie": {
        "master": REVISION / "masters/genie-matte.png",
        "mask": REVISION / "mattes/genie.png",
    },
    "masterGenie": {
        "master": REVISION / "masters/masterGenie-matte-v2.png",
        "mask": REVISION / "mattes/masterGenie-v2.png",
    },
    "naga": {
        "master": REVISION / "masters/naga-matte.png",
        "mask": REVISION / "mattes/naga.png",
    },
    "nagaQueen": {
        "master": REVISION / "masters/nagaQueen-matte-v2.png",
        "mask": REVISION / "mattes/nagaQueen-v2.png",
    },
    "giant": {
        "master": REVISION / "masters/giant-matte.png",
        "mask": REVISION / "mattes/giant.png",
    },
    "titan": {
        "master": REVISION / "masters/titan-matte.png",
        "mask": REVISION / "mattes/titan.png",
    },
    "titanV2": {
        "master": REVISION / "masters/titan-matte-v2.png",
        "mask": REVISION / "mattes/titan-v2.png",
    },
    "titanV3": {
        "master": REVISION / "masters/titan-matte-v3.png",
        "mask": REVISION / "mattes/titan-v3.png",
    },
}


def render_matte(master: Path) -> Image.Image:
    """Return the pinned LANCZOS reduction and binary threshold result."""
    with Image.open(master) as source:
        reduced = source.convert("L").resize(SIZE, Image.Resampling.LANCZOS)
    return reduced.point(lambda value: 255 if value >= THRESHOLD else 0, mode="L")


def export(creature: str, check: bool, parser: argparse.ArgumentParser) -> None:
    paths = CREATURES[creature]
    master = paths["master"]
    mask = paths["mask"]
    if not master.is_file():
        parser.error(f"missing {creature} geometry master: {master}")
    expected = render_matte(master)

    if check:
        if not mask.is_file():
            parser.error(f"missing {creature} native mask: {mask}")
        with Image.open(mask) as current:
            if current.mode != "L" or current.size != SIZE:
                parser.error(f"{creature} mask must be grayscale {SIZE[0]}x{SIZE[1]}: {current.mode} {current.size}")
            actual_pixels = current.tobytes()
        if actual_pixels != expected.tobytes():
            parser.error(f"{creature} mask differs from the mechanical master reduction")
        digest = hashlib.sha256(mask.read_bytes()).hexdigest()
        print(f"PASS: {creature} mask matches LANCZOS {SIZE[0]}x{SIZE[1]} threshold >= {THRESHOLD}; sha256={digest}")
        return

    mask.parent.mkdir(parents=True, exist_ok=True)
    expected.save(mask, format="PNG")
    digest = hashlib.sha256(mask.read_bytes()).hexdigest()
    print(f"Wrote {mask.relative_to(ROOT)} ({SIZE[0]}x{SIZE[1]} L, mechanical draft); sha256={digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--creature",
        choices=tuple(CREATURES),
        help="named geometry master to export/check (default: gremlin)",
    )
    selection.add_argument(
        "--all",
        action="store_true",
        help="export/check all currently available named masters (not an approval)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify selected native mask(s) without writing them",
    )
    args = parser.parse_args()
    selected = tuple(CREATURES) if args.all else (args.creature or "gremlin",)
    for creature in selected:
        export(creature, args.check, parser)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
