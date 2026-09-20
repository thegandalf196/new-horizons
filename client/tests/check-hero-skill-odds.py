#!/usr/bin/env python3
"""Source/layout guard, not a rendered-UI or probability-engine test."""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[2]
hero = (root / "client/windows/CHeroWindow.cpp").read_text()
window = (root / "client/windows/HeroSkillOddsWindow.cpp").read_text()
assert "createAndPushWindow<HeroSkillOddsWindow>(*curHero)" in hero
assert "createAndPushWindow<HeroGrowthWindow>" not in hero
assert "Open Hero development for the saved growth profile" not in hero
assert 'move(growthButton, Point(402, 166))' in hero
data = (root / "Mods/new-horizons/Images/NH_hero_growth_entry_normal.png").read_bytes()
width, height = struct.unpack(">II", data[16:24])
assert 166 + height <= 192, "Button overlaps first skill row"
assert 402 + width <= 440, "Button leaves skills panel"
assert 'hero.getPrimaryGrowthRules()' in window
assert 'newHorizonsHeroes::usesSkillOfferWeights(rules)' in window
assert 'rules["skillOfferWeights"].Struct()' in window
assert 'hero.getHeroClass()->secSkillProbability' in window
assert 'std::make_shared<CTextBox>' in window
assert 'Not exact next-level probabilities.' in window
assert 'base share ' in window
assert 'const CGHeroInstance & hero' in window
print("Hero skill odds binding, saved-weight source, disclaimer and layout guard PASS")
