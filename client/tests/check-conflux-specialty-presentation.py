#!/usr/bin/env python3
"""Supplemental source/selected-art binding check; native API tests are separate."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
from nhart_test_resources import open_image

rules = json.loads((ROOT / "config/newHorizonsHeroes.json").read_text())
assert rules["defaultCreatureLineReplacements"] == {
    "core:pasis": "new-horizons:wisp", "core:monere": "new-horizons:wisp"
}
for resource in ("SPRITES/Wisp/icons/wisp-icon-32.png",
                 "SPRITES/WispUpgrade/icons/icon-32x32.png"):
    with open_image(resource) as image:
        assert image.size == (32, 32), (resource, image.size)
for relative in ("client/windows/CHeroWindow.cpp", "client/windows/CExchangeWindow.cpp",
                 "client/windows/CKingdomInterface.cpp"):
    assert "heroSpecialtyPresentation" in (ROOT / relative).read_text(), relative
for relative in ("client/windows/CHeroOverview.cpp", "client/lobby/OptionsTab.cpp",
                 "client/windows/wiki/WikiHeroContent.cpp", "client/windows/wiki/WikiTownContent.cpp"):
    assert ".specialty" in (ROOT / relative).read_text(), relative
print("PASS: two captured default identities, both selected 32px line icons, seven UI consumers")
