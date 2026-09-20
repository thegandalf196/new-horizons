#!/usr/bin/env python3
"""Static regression check for the Time Stop battle-status presentation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
HOVER_PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
STACK_WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text(encoding="utf-8")
SPELLS = (ROOT / "Mods/new-horizons/Content/config/spells/newHorizons.json").read_text(encoding="utf-8")


class TimeStopStatusUiSourceTest(unittest.TestCase):
    def test_time_stop_uses_shared_badge_and_explicit_remaining_semantics(self):
        self.assertIn('TIME_STOP_SPELL_KEY = "new-horizons:timeStop"', STATUS)
        self.assertIn('TIME_STOP_BADGE = "ST"', STATUS)
        self.assertIn("Remaining: until the beginning of the caster's next Hero Action.", STATUS)
        self.assertIn("NewHorizonsBattleStatus.h", HOVER_PANEL)
        self.assertIn("NewHorizonsBattleStatus.h", STACK_WINDOW)
        self.assertIn("TIME_STOP_BADGE", HOVER_PANEL)
        self.assertIn("TIME_STOP_BADGE", STACK_WINDOW)
        self.assertIn("timeStopTooltip", HOVER_PANEL)
        self.assertIn("timeStopTooltip", STACK_WINDOW)
        self.assertIn("std::stable_partition(spells.begin(), spells.end()", HOVER_PANEL)
        self.assertIn("std::stable_partition(spells.begin(), spells.end()", STACK_WINDOW)
        self.assertLess(
            HOVER_PANEL.index("std::stable_partition(spells.begin(), spells.end()"),
            HOVER_PANEL.index("for(SpellID effect : spells)"),
        )
        self.assertLess(
            STACK_WINDOW.index("std::stable_partition(spells.begin(), spells.end()"),
            STACK_WINDOW.index("for(SpellID effect : spells)"),
        )

    def test_other_spell_duration_presentation_remains_intact(self):
        self.assertIn("std::to_string(duration)", HOVER_PANEL)
        self.assertIn("std::to_string(duration)", STACK_WINDOW)
        self.assertIn("spellDurationRemaining", STACK_WINDOW)
        self.assertIn('effect.getNum() + 1', HOVER_PANEL)
        self.assertIn('effect + 1', STACK_WINDOW)
        self.assertIn("printed >= 3 || (printed == 2 && spells.size() > 3)", HOVER_PANEL)
        self.assertIn("printed >= 8", STACK_WINDOW)

    def test_time_stop_keeps_existing_registered_placeholder_icon(self):
        self.assertIn('"iconEffect" : "SPELLINT.def:0:16"', SPELLS)
        self.assertIn('"iconImmune" : "SPELLINT.def:0:16"', SPELLS)


if __name__ == "__main__":
    unittest.main()
