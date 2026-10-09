#!/usr/bin/env python3
"""Source guard for the compact, truthful temporary-creature status."""

from pathlib import Path
import unittest
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
from nhart_test_resources import ArtPath
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "client/battle/StackInfoStatusPresentation.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
PANEL_H = (ROOT / "client/battle/StackInfoBasicPanel.h").read_text(encoding="utf-8")
PERK_ICONS = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(encoding="utf-8")
REANIMATE_ICON = ArtPath() / "NH_spell_reanimate_30.png"


class TemporaryCreatureStatusUiTest(unittest.TestCase):
    def test_status_snapshot_uses_serialized_current_temporary_count(self):
        self.assertIn("TemporaryCreatureStatus temporaryCreatures;", STATUS)
        self.assertIn("makeTemporaryCreatureStatus(\n\t\t\tstack->health.getResurrected())", PANEL)
        self.assertIn("return {std::max<int32_t>(0, resurrectedCount)};", PRESENTATION)
        self.assertIn("bool active() const\n\t{\n\t\treturn remainingCount > 0;", PRESENTATION)
        self.assertIn("temporaryCreatureIcons.clear();", PANEL)

    def test_compact_indicator_reuses_authored_reanimate_art_and_existing_slot(self):
        self.assertIn("StackStatusIconKind::TEMPORARY_CREATURES", PRESENTATION)
        self.assertIn("StackStatusIconKind::TEMPORARY_CREATURES: return 2", PRESENTATION)
        self.assertIn('ImagePath::builtin("NH_spell_reanimate_30.png")', PANEL)
        self.assertTrue(REANIMATE_ICON.is_file())
        self.assertIn("Point(slotX + 9, slotY + 3)", PANEL)
        self.assertIn("TextOperations::formatMetric(temporaryCreatures.remainingCount, 4)", PANEL)
        self.assertIn("std::vector<std::shared_ptr<CPicture>> temporaryCreatureIcons", PANEL_H)

    def test_right_click_help_is_generic_and_explains_battle_expiry(self):
        tooltip = STATUS[STATUS.index("inline std::string temporaryCreatureTooltip"):
                         STATUS.index("inline std::string vampirismTooltip")]
        self.assertIn('"Temporary\\n"', tooltip)
        self.assertIn("temporary creature", tooltip)
        self.assertIn("fight normally during this battle", tooltip)
        self.assertIn("disappear when the battle ends", tooltip)
        self.assertIn("temporaryCreatureTooltip(", PANEL)
        self.assertIn("Rect(slotX, slotY, 48, 36), tooltip, tooltip", PANEL)

    def test_reanimator_uses_its_purpose_made_perk_icon(self):
        self.assertIn('{"new-horizons:shadowMagic.reanimator", "NH_perk_reanimator"}', PERK_ICONS)

    def test_status_refresh_tracks_count_and_uses_one_generic_entry(self):
        self.assertIn("if(current == displayedStatus", PANEL)
        self.assertIn("temporaryCreatures.active()", PANEL)
        self.assertIn("newHorizonsBattleStatus::StackStatusIconKind::TEMPORARY_CREATURES, std::nullopt", PANEL)
        self.assertIn("spells.size() - hiddenReanimateSpellEffects", PANEL)
        self.assertIn("One-battle resurrected creatures are shown once as a generic temporary", PANEL)


if __name__ == "__main__":
    unittest.main()
