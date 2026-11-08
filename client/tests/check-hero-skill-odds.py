#!/usr/bin/env python3
"""Source/layout guard, not a rendered-UI or probability-engine test."""
from pathlib import Path
import struct
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools/tests"))
from nhart_test_resources import read_resource
hero = (root / "client/windows/CHeroWindow.cpp").read_text()
layout = (root / "client/windows/CHeroWindowLayout.cpp").read_text()
window = (root / "client/windows/HeroSkillOddsWindow.cpp").read_text()
assert "createAndPushWindow<HeroSkillOddsWindow>(*curHero)" in hero
assert "createAndPushWindow<HeroGrowthWindow>" not in hero
assert "Open Hero development for the saved growth profile" not in hero
assert 'Colors::YELLOW, "Skills / learned perks", 376' in layout
assert 'move(growthButton, Point(14 + static_cast<int>(skillsHeading->getWidth()), 176))' in layout
assert 'growthButton->setImage(AnimationPath::builtin("NH_info_button_small"))' in hero
assert "class HeroSkillOddsInfoMark" not in hero, "Do not restore the floating vector mark"
assert 'movementArea = std::make_shared<LRClickableAreaWText>(Rect(152, 132, 140, 44)' in layout
assert 'legacySiegeArea = std::make_shared<LRClickableAreaWText>(Rect(728, 12, 64, 76)' in layout
# Siege occupies the third top-right stat column after Morale and Luck.
assert 'move(morale, Point(579, 33))' in layout
assert 'move(luck, Point(661, 33))' in layout
assert 'AnimationPath::builtin("NH_capability_siege_32")' in layout
assert 'Rect(744, 40, 32, 32)' in layout
assert 661 + 53 <= 728 and 744 + 32 <= 800
data = read_resource("SPRITES/NH_hero_growth_entry_normal.png")
width, height = struct.unpack(">II", data[16:24])
assert (width, height) == (24, 24), "Legacy entry-control frame size changed"
for resource, size in (("SPRITES/NH_info_button_small_normal.png", 16),
                       ("SPRITES/NH_info_button_normal.png", 22),
                       ("SPRITES/NH_hero_temporary_effects_icon.png", 24)):
    image = read_resource(resource)
    assert struct.unpack(">II", image[16:24]) == (size, size), resource
info_mark_size = 16
button_y = 176
upper_row_bottom = 132 + 44
first_skill_row = 192
assert button_y >= upper_row_bottom, "Button overlaps the Movement row"
assert button_y + info_mark_size <= first_skill_row, "Button overlaps first skill row"
assert 14 + 376 + info_mark_size <= 440, "Button leaves skills panel at maximum heading width"
assert 'hero.getPrimaryGrowthRules()' in window
assert 'newHorizonsHeroes::usesSkillOfferWeights(rules)' in window
assert 'rules["skillOfferWeights"].Struct()' in window
assert 'hero.getHeroClass()->secSkillProbability' in window
assert 'std::make_shared<CTextBox>' in window
assert 'Not exact next-level probabilities.' in window
assert 'Base share ' in window
assert 'const CGHeroInstance & hero' in window
print("Hero skill odds binding, saved-weight source, disclaimer and layout guard PASS")
