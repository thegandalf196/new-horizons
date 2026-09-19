#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "assets/new-horizons/approved-six-school-assets.json"
IMAGES = ROOT / "Mods/new-horizons/Images"
SCHOOLS = ("light", "nature", "sorcery", "havoc", "shadow", "chaos")


def main():
    manifest = json.loads(MANIFEST.read_text())
    expected = manifest["files"]
    assert len(expected) == 162
    assert sum(name.endswith(".png") for name in expected) == 144
    assert sum(name.endswith(".json") for name in expected) == 18

    for name, expected_hash in expected.items():
        path = IMAGES / name
        assert path.is_file(), f"Missing approved school art: {name}"
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        assert actual_hash == expected_hash, f"Approved school art changed: {name}"

    schools = json.loads((ROOT / "config/newHorizonsSchools.json").read_text())
    module = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())
    for school in SCHOOLS:
        border = f"NH_{school}_spellBorders.json"
        assert schools[school]["schoolBorders"] == border
        assert module["spellSchools"][school]["schoolBorders"] == border

    generator = (ROOT / "assets/new-horizons/generate_icons.py").read_text()
    assert "preserve_approved(output)" in generator

    print("PASS: all 162 approved school-art assets and six live border bindings")


if __name__ == "__main__":
    main()
