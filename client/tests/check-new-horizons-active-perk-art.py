#!/usr/bin/env python3
"""Focused source/runtime guard for the active-perk v2 paintings."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE_V2 = ROOT / "assets/new-horizons/art-source/active-perks-v2"
SOURCE_V3 = ROOT / "assets/new-horizons/art-source/active-perks-v3"
IMAGES = ROOT / "Mods/new-horizons/Images"
ICONS = ROOT / "client/windows/NewHorizonsPerkIcons.h"
DEFINITIONS = ROOT / "config/newHorizonsPerks.json"

V2_EXPECTED = {
    "new-horizons:discipline.inspirationalLeader": (
        "NH_perk_inspirational_leader",
        "inspirational-leader",
    ),
    "new-horizons:sylvanLuck.wildChance": (
        "NH_perk_wild_chance",
        "wild-chance",
    ),
    "new-horizons:sylvanLuck.perfectMoment": (
        "NH_perk_perfect_moment",
        "perfect-moment",
    ),
}
V3_EXPECTED = {
    "new-horizons:offense.shockAssault": (
        "NH_perk_shock_assault",
        "shock-assault",
    ),
}
EXPECTED = V2_EXPECTED | V3_EXPECTED


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def main() -> None:
    mapping = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', ICONS.read_text(encoding="utf-8")))
    for source, expected in ((SOURCE_V2, V2_EXPECTED), (SOURCE_V3, V3_EXPECTED)):
        generation = json.loads((source / "generation.json").read_text(encoding="utf-8"))
        by_id = {asset["id"]: asset for asset in generation["assets"]}
        assert set(by_id) == set(expected), (source, "generation coverage")
        assert generation["status"].startswith("provisional"), "provisional art must be labeled honestly"
        for perk_id, (key, slug) in expected.items():
            asset = by_id[perk_id]
            master = source / asset["master"]
            assert master.is_file(), master
            assert image_size(master) == tuple(asset["dimensions"]) == (1254, 1254)
            assert digest(master) == asset["source_sha256"], (perk_id, "master hash")
            prompt = source / asset["prompt_file"]
            assert prompt.is_file() and prompt.read_text(encoding="utf-8").strip(), prompt

            export = source / "exports" / slug
            for filename in (
                "master.png",
                f"{slug}-44.png",
                f"{slug}-32.png",
                f"{slug}-comparison.png",
                f"{slug}-manifest.json",
            ):
                assert (export / filename).is_file(), export / filename
            assert image_size(export / f"{slug}-44.png") == (44, 44)
            assert image_size(export / f"{slug}-32.png") == (32, 32)

            assert mapping.get(perk_id) == key, (perk_id, mapping.get(perk_id), key)
            descriptor = json.loads((IMAGES / f"{key}.json").read_text(encoding="utf-8"))
            frames = descriptor["images"]
            assert [frame["frame"] for frame in frames] == [0, 1, 2, 3]
            assert [frame["file"] for frame in frames] == [
                f"{key}_normal.png",
                f"{key}_pressed.png",
                f"{key}_disabled.png",
                f"{key}_highlighted.png",
            ]
            for frame in frames:
                path = IMAGES / frame["file"]
                assert path.is_file(), path
                assert image_size(path) == (44, 44)

    definitions = json.loads(DEFINITIONS.read_text(encoding="utf-8"))
    active = {
        perk["id"]
        for skill in definitions["skills"].values()
        for perk in skill["perks"]
        if perk["effect"]["status"] == "active"
    }
    assert set(EXPECTED) <= active
    normal_hashes = set()
    for perk_id in sorted(active):
        key = mapping[perk_id]
        descriptor = json.loads((IMAGES / f"{key}.json").read_text(encoding="utf-8"))
        normal = IMAGES / descriptor["images"][0]["file"]
        assert image_size(normal) == (44, 44)
        value = digest(normal)
        assert value not in normal_hashes, f"duplicate active normal art: {perk_id}"
        normal_hashes.add(value)

    print(f"PASS: active-perk sources, previews, runtime states, bindings, and {len(active)}-icon uniqueness")


if __name__ == "__main__":
    main()
