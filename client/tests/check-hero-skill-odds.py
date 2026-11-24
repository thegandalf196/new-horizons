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
stat_row = (root / "client/windows/HeroWindowStatRow.cpp").read_text()
window = (root / "client/windows/HeroSkillOddsWindow.cpp").read_text()
assert "createAndPushWindow<HeroSkillOddsWindow>(*curHero)" in hero
assert "createAndPushWindow<HeroGrowthWindow>" not in hero
assert "Open Hero development for the saved growth profile" not in hero
assert 'Colors::YELLOW, "Skills / learned perks", 376' in layout
assert 'move(growthButton, Point(14 + static_cast<int>(skillsHeading->getWidth()), heroStatRow::SKILLS_TOP + 1))' in layout
assert 'growthButton->setImage(AnimationPath::builtin("NH_info_button_small"))' in hero
assert "class HeroSkillOddsInfoMark" not in hero, "Do not restore the floating vector mark"
assert 'movementArea = std::make_shared<LRClickableAreaWText>(cellRect(Cell::MOVEMENT)' in stat_row
assert 'legacySiegeArea = std::make_shared<LRClickableAreaWText>(Rect(674, 12, 54, 76)' in layout
# Siege occupies the third top-right stat column after Morale and Luck.
assert 'move(morale, Point(576, 30))' in layout
assert 'move(luck, Point(631, 30))' in layout
assert 'AnimationPath::builtin("NH_capability_siege_32")' in layout
assert 'Rect(686, 36, 30, 30)' in layout
assert 631 + 30 <= 674 and 686 + 30 <= 800
data = read_resource("SPRITES/NH_hero_growth_entry_normal.png")
width, height = struct.unpack(">II", data[16:24])
assert (width, height) == (24, 24), "Legacy entry-control frame size changed"
for resource, size in (("SPRITES/NH_info_button_small_normal.png", 16),
                       ("SPRITES/NH_info_button_normal.png", 22),
                       ("SPRITES/NH_hero_temporary_effects_icon.png", 24)):
    image = read_resource(resource)
    assert struct.unpack(">II", image[16:24]) == (size, size), resource
info_mark_size = 16
# heroStatRow: one 38 px stat row at y=136; the skills panel starts 6 px below it.
button_y = 136 + 38 + 6 + 1
upper_row_bottom = 136 + 38
first_skill_row = 136 + 38 + 6 + 18
assert button_y >= upper_row_bottom, "Button overlaps the stat row"
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
# Same tiled leather surface as the perk browser; no per-skill card boxes.
assert 'std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h))' in window
assert 'TransparentFilledRectangle' not in window, "Skill entries sit directly on the dialog surface"
# Left-click opens the Skill's perk catalogue, as a learned Skill does in the hero window.
assert 'createAndPushWindow<NewHorizonsPerkBrowser>(*hero, skillId)' in window
assert 'skillId = skill.toSkill()->getJsonKey();' in window
assert 'newHorizonsPerkHelp::skillDefinition(hero, skillId)' in window
assert 'LRClickableAreaWTextComp::clickPressed(cursorPosition)' in window, "Skills without a catalogue keep their info dialog"
print("Hero skill odds binding, saved-weight source, disclaimer and layout guard PASS")
