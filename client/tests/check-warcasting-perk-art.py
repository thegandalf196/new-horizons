#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the three provisional Warcasting perk paintings and runtime bindings."""

import hashlib
import json
from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/warcasting-perks-v1"
IMAGES = ROOT / "Mods/new-horizons/Images"
EXPECTED = {
    "martialChanneling": ("martial-channeling", "NH_perk_martial_channeling"),
    "arcaneChanneling": ("arcane-channeling", "NH_perk_arcane_channeling"),
    "tacticalWeaving": ("tactical-weaving", "NH_perk_tactical_weaving"),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def size(path):
    with Image.open(path) as image:
        return image.size


generation = json.loads((SOURCE / "generation.json").read_text())
runtime = json.loads((SOURCE / "runtime-manifest.json").read_text())
assert generation["status"].startswith("provisional")
assert (SOURCE / generation["prompt_file"]).read_text().strip()
assets = {asset["id"]: asset for asset in generation["assets"]}
bindings = dict(re.findall(
    r'\{"(new-horizons:[^"]+)", "([^"]+)"\}',
    (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(),
))
registry = json.loads((ROOT / "config/newHorizonsPerks.json").read_text())
perks = {perk["id"]: perk for perk in registry["skills"]["new-horizons:warcasting"]["perks"]}
assert set(assets) == {"new-horizons:warcasting." + key for key in EXPECTED}
for key, (slug, stem) in EXPECTED.items():
    perk_id = "new-horizons:warcasting." + key
    assert perks[perk_id]["effect"]["status"] == "active", perk_id
    assert bindings[perk_id] == stem
    asset = assets[perk_id]
    master = SOURCE / asset["master"]
    assert digest(master) == asset["source_sha256"]
    assert size(master) == tuple(asset["dimensions"])
    for pixels in (44, 32):
        assert size(SOURCE / "exports" / slug / f"{slug}-{pixels}.png") == (pixels, pixels)
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

print("PASS: three Warcasting perk paintings, runtime states, hashes, and bindings")
