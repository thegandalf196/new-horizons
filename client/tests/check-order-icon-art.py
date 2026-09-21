#!/usr/bin/env python3
"""Validate the distinct provisional art used by every New Horizons Order."""

import hashlib
import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "Mods/new-horizons/Images"
SOURCES = ROOT / "assets/new-horizons/art-source/orders-v1"
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


def assert_icon(path: Path) -> None:
    with Image.open(path) as image:
        assert image.size == (64, 64), f"{path.name}: expected 64x64"
        assert image.mode == "RGBA", f"{path.name}: expected RGBA"
        low, high = image.getchannel("A").getextrema()
        assert low < high and high > 0, f"{path.name}: transparency/opaque subject missing"
        assert len(image.getcolors(maxcolors=1_000_000) or ()) > 24, f"{path.name}: flat placeholder"


def main() -> None:
    runtime_normal_hashes = []
    master_hashes = []
    for slug, stem in ORDERS.items():
        source = SOURCES / slug
        assert (source / "prompt.txt").is_file(), f"{slug}: missing generation prompt"
        master = source / "master.png"
        assert master.is_file(), f"{slug}: missing master"
        master_hashes.append(hashlib.sha256(master.read_bytes()).hexdigest())
        for suffix in ("44", "32"):
            with Image.open(source / f"{slug}-{suffix}.png") as image:
                assert image.size == (int(suffix), int(suffix)), f"{slug}: bad {suffix}px export"
                assert image.mode == "RGBA", f"{slug}: {suffix}px export is not RGBA"

        descriptor = json.loads((IMAGES / f"{stem}_button.json").read_text(encoding="utf-8"))
        assert descriptor == {
            "images": [
                {"group": 0, "frame": index, "file": f"{stem}_{state}.png"}
                for index, state in enumerate(STATES)
            ]
        }, f"{stem}: bad animation descriptor"
        for state in STATES:
            assert_icon(IMAGES / f"{stem}_{state}.png")
        assert_icon(IMAGES / f"{stem}_icon.png")
        runtime_normal_hashes.append(hashlib.sha256((IMAGES / f"{stem}_normal.png").read_bytes()).hexdigest())

    assert len(set(master_hashes)) == len(ORDERS), "duplicate Order masters"
    assert len(set(runtime_normal_hashes)) == len(ORDERS), "duplicate normal Order icons"
    action = (ROOT / "client/battle/BattleHeroActionWindow.cpp").read_text(encoding="utf-8")
    assert "NH_hero_actions_entry" not in action, "generic hero-action placeholder still bound"
    for stem in ORDERS.values():
        assert f'"{stem}_button"' in action, f"{stem}: client binding missing"
    print("PASS: eight distinct Order masters, 44/32 previews, 64px runtime states and client bindings")


if __name__ == "__main__":
    main()
