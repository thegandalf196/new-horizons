#!/usr/bin/env python3
"""Source guard for active Crusade bonus readback in the battle stack panel."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "client/battle/StackInfoStatusPresentation.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")


class CrusadeStatusUiSourceTest(unittest.TestCase):
	def test_status_readback_uses_crusade_timed_bonus_values(self):
		self.assertIn('CRUSADE_SPELL_KEY = "new-horizons:crusade"', STATUS)
		helper = STATUS[STATUS.index("CrusadeStatus crusadeStatus("):STATUS.index("inline std::string crusadeTooltip(")]
		for term in (
			"BonusSource::SPELL_EFFECT",
			"BonusDuration::N_TURNS",
			"bonus->turnsRemain <= 0",
			"CRUSADE_SPELL_KEY",
			"BonusType::PRIMARY_SKILL",
			"PrimarySkill::ATTACK",
			"PrimarySkill::DEFENSE",
			"BonusType::STACKS_INITIATIVE_FLAT",
			"BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS",
			"SpellSchool::ANY",
			"BonusType::MINIMUM_MORALE",
			"result.protectsMoraleFromNegative = true",
		):
			self.assertIn(term, helper)
		self.assertIn("std::min(result.remainingRounds", helper)

	def test_existing_stack_spell_slot_shows_actual_values_and_remaining_rounds(self):
		tooltip = STATUS[STATUS.index("inline std::string crusadeTooltip("):STATUS.index("struct GuardianSpiritStatus")]
		for term in (
			"status.attackBonus",
			"status.defenseBonus",
			"status.initiativeBonus",
			"status.magicalDamageReductionBasisPoints",
			"status.protectsMoraleFromNegative",
			"roundsRemaining(status.remainingRounds)",
		):
			self.assertIn(term, tooltip)

		self.assertIn("StackStatusIconKind::CRUSADE", PRESENTATION)
		self.assertIn("StackStatusIconKind::CRUSADE: return 2", PRESENTATION)
		self.assertIn("newHorizonsBattleStatus::crusadeStatus(*spellBonuses)", PANEL)
		self.assertIn("newHorizonsBattleStatus::crusadeTooltip(", PANEL)
		self.assertIn('AnimationPath::builtin("SpellInt"), effect.getNum() + 1', PANEL)
		self.assertIn("Rect(slotX, slotY, 48, 36), tooltip, tooltip", PANEL)


if __name__ == "__main__":
	unittest.main()
