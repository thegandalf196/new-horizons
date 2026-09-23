#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate provisional Warcasting perk paintings and runtime bindings."""

import hashlib
import json
from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "assets/new-horizons/art-source"
IMAGES = ROOT / "Mods/new-horizons/Images"
EXPECTED = {
    "martialChanneling": ("warcasting-perks-v1", "martial-channeling", "NH_perk_martial_channeling"),
    "arcaneChanneling": ("warcasting-perks-v1", "arcane-channeling", "NH_perk_arcane_channeling"),
    "tacticalWeaving": ("warcasting-perks-v1", "tactical-weaving", "NH_perk_tactical_weaving"),
    "battleMeditation": ("battle-meditation-v1", "battle-meditation", "NH_perk_battle_meditation"),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def size(path):
    with Image.open(path) as image:
        return image.size


bindings = dict(re.findall(
    r'\{"(new-horizons:[^"]+)", "([^"]+)"\}',
    (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(),
))
registry = json.loads((ROOT / "config/newHorizonsPerks.json").read_text())
perks = {perk["id"]: perk for perk in registry["skills"]["new-horizons:warcasting"]["perks"]}
for key, (directory, slug, stem) in EXPECTED.items():
    source = SOURCES / directory
    generation = json.loads((source / "generation.json").read_text())
    runtime = json.loads((source / "runtime-manifest.json").read_text())
    assert generation["status"].startswith("provisional")
    assert (source / generation["prompt_file"]).read_text().strip()
    assets = {asset["id"]: asset for asset in generation["assets"]}
    assert set(assets) == {
        "new-horizons:warcasting." + name
        for name, spec in EXPECTED.items() if spec[0] == directory
    }
    perk_id = "new-horizons:warcasting." + key
    assert perks[perk_id]["effect"]["status"] == "active", perk_id
    assert bindings[perk_id] == stem
    asset = assets[perk_id]
    master = source / asset["master"]
    assert digest(master) == asset["source_sha256"]
    assert size(master) == tuple(asset["dimensions"])
    for pixels in (44, 32):
        assert size(source / "exports" / slug / f"{slug}-{pixels}.png") == (pixels, pixels)
    descriptor_path = IMAGES / f"{stem}.json"
    assert digest(descriptor_path) == runtime["files"][descriptor_path.name]
    frames = json.loads(descriptor_path.read_text())["images"]
    assert [frame["frame"] for frame in frames] == [0, 1, 2, 3]
    assert [frame["file"] for frame in frames] == [
        f"{stem}_{state}.png" for state in ("normal", "pressed", "disabled", "highlighted")
    ]
    for frame in frames:
        path = IMAGES / frame["file"]
        assert size(path) == (44, 44)
        assert digest(path) == runtime["files"][path.name]

print("PASS: four Warcasting perk paintings, runtime states, hashes, and bindings")
