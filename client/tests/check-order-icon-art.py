#!/usr/bin/env python3
"""Validate the distinct provisional art used by every New Horizons Order."""

import hashlib
import json
import argparse
from pathlib import Path
import re
import sys

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
from nhart_test_resources import ArtPath
IMAGES = ArtPath()
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


def assert_icon(path: ArtPath) -> None:
    with path.open_image() as image:
        assert image.size == (64, 64), f"{path.name}: expected 64x64"
        assert image.mode == "RGBA", f"{path.name}: expected RGBA"
        low, high = image.getchannel("A").getextrema()
        assert low < high and high > 0, f"{path.name}: transparency/opaque subject missing"
        assert len(image.getcolors(maxcolors=1_000_000) or ()) > 24, f"{path.name}: flat placeholder"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path,
                        help="Optional private orders-v1 directory for master/export byte checks")
    args = parser.parse_args()
    runtime_normal_hashes = []
    master_hashes = []
    for slug, stem in ORDERS.items():
        source = SOURCES / slug
        assert (source / "prompt.txt").is_file(), f"{slug}: missing generation prompt"
        # Public provenance stays auditable without requiring private artwork.
        manifest = json.loads((source / f"{slug}-manifest.json").read_text())
        outputs = {item["file"]: item for item in manifest["outputs"]}
        master = outputs["master.png"]
        assert master["sha256"] == manifest["source"]["sha256"]
        assert master["dimensions"] == manifest["source"]["dimensions"] == [1254, 1254]
        assert re.fullmatch(r"[0-9a-f]{64}", master["sha256"])
        master_hashes.append(master["sha256"])
        for suffix in ("44", "32"):
            item = outputs[f"{slug}-{suffix}.png"]
            assert item["dimensions"] == [int(suffix), int(suffix)], f"{slug}: bad {suffix}px export"
            assert item["mode"] == "RGBA", f"{slug}: {suffix}px export is not RGBA"
            assert re.fullmatch(r"[0-9a-f]{64}", item["sha256"])
        if args.source_root is not None:
            for item in manifest["outputs"]:
                private = args.source_root / slug / item["file"]
                assert hashlib.sha256(private.read_bytes()).hexdigest() == item["sha256"], private
                with Image.open(private) as image:
                    assert list(image.size) == item["dimensions"], private
                    if "mode" in item:
                        assert image.mode == item["mode"], private

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
    print("PASS: eight distinct Order provenance hashes, 44/32 export metadata, packaged 64px runtime states and client bindings"
          + ("; private source bytes verified" if args.source_root else ""))


if __name__ == "__main__":
    main()
