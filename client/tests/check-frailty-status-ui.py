#!/usr/bin/env python3
"""Source guard for battle-long Frailty stack-status presentation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")


class FrailtyStatusUiSourceTest(unittest.TestCase):
    def test_cumulative_strength_uses_scalar_add_info_with_defense_delta_fallback(self):
        helper = PANEL[PANEL.index("currentFrailtyStatus("):PANEL.index("std::string frailtyTooltip(")]
        self.assertIn('FRAILTY_SPELL_KEY = "new-horizons:frailty"', PANEL)
        self.assertIn("bonus->parameters->toNumber()", helper)
        self.assertIn("basisPoints < 0 || basisPoints > 6000", helper)
        self.assertIn("bonus->type != BonusType::PRIMARY_SKILL", helper)
        self.assertIn("BonusSubtypeID(PrimarySkill::DEFENSE)", helper)
        self.assertIn("bonus->val > 0", helper)
        self.assertIn("defenseLoss * 10000 + baseDefense / 2", helper)
        self.assertIn("std::clamp<int64_t>(basisPoints, 0, 6000)", helper)

    def test_frailty_shows_percent_badge_and_battle_long_hover_without_countdown(self):
        badge = PANEL[PANEL.index("if(settings[\"general\"][\"enableUiEnhancements\"]"):
                            PANEL.index("if(timeStop)", PANEL.index("if(settings[\"general\"][\"enableUiEnhancements\"]"))]
        tooltip = PANEL[PANEL.index("std::string frailtyTooltip("):PANEL.index("newHorizonsBattleStatus::DefendStatus currentDefendStatus(")]
        self.assertIn("formatBasisPoints(frailty->accumulatedBasisPoints)", badge)
        self.assertLess(badge.index("frailty ?"), badge.index("std::to_string(duration)"))
        frailty_badge = badge[badge.index("frailty ?"):badge.index(": std::to_string(duration)")]
        self.assertNotIn("duration", frailty_badge)
        self.assertIn("Battle-long; there is no duration counter.", tooltip)
        self.assertIn("Dispel removes the accumulated effect.", tooltip)
        self.assertIn("Current Creature Defense penalty:", tooltip)

    def test_frailty_reuses_the_existing_spell_slot_and_hover_area(self):
        self.assertIn('AnimationPath::builtin("SpellInt"), effect.getNum() + 1', PANEL)
        self.assertIn("const auto frailty = currentFrailtyStatus(stack, spellKey, spellBonuses);", PANEL)
        self.assertIn("Rect(slotX, slotY, 48, 36), tooltip, tooltip", PANEL)
        self.assertIn("std::to_string(duration)", PANEL)


if __name__ == "__main__":
    unittest.main()
