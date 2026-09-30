#!/usr/bin/env python3
"""Focused source guard for existing-stack Entangle status readback."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "client/battle/StackInfoStatusPresentation.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
NATIVE_TEST = (ROOT / "test/client/NewHorizonsEntangleStatusTest.cpp").read_text(encoding="utf-8")


class EntangleStatusUiSourceTest(unittest.TestCase):
	def test_readback_requires_the_spell_bound_timed_bind_marker(self):
		self.assertIn('ENTANGLE_SPELL_KEY = "new-horizons:entangle"', STATUS)
		helper = STATUS[STATUS.index("EntangleStatus entangleStatus("):STATUS.index("inline std::string entangleTooltip(")]
		for term in (
			"BonusType::BIND_EFFECT",
			"BonusSource::SPELL_EFFECT",
			"BonusDuration::N_TURNS",
			"bonus->turnsRemain <= 0",
			"bonus->parameters",
			"bonus->sid.toString() != ENTANGLE_SPELL_KEY",
		):
			self.assertIn(term, helper)
		self.assertIn('EXPECT_FALSE(isEntangle("core:bind"))', NATIVE_TEST)
		self.assertIn("DoesNotReadLegacyBindAsEntangle", NATIVE_TEST)

	def test_the_existing_spell_slot_uses_actual_remaining_rounds(self):
		self.assertIn("EntangleStatus entangle;", STATUS)
		self.assertIn("result.entangle = newHorizonsBattleStatus::entangleStatus(*spellBonuses)", PANEL)
		self.assertIn("StackStatusIconKind::ENTANGLE", PRESENTATION)
		self.assertIn("StackStatusIconKind::ENTANGLE: return 1", PRESENTATION)
		self.assertIn("std::to_string(displayedStatus.entangle.remainingRounds)", PANEL)
		self.assertIn('AnimationPath::builtin("SpellInt"), effect.getNum() + 1', PANEL)

	def test_help_distinguishes_entangle_from_time_stop_and_states_allowed_actions(self):
		helper = STATUS[STATUS.index("inline std::string entangleTooltip("):STATUS.index("inline bool isGuardianSpirit(")]
		for term in (
			"Unlike Time Stop",
			"Initiative and activation timing are unchanged",
			"attack adjacent enemies, retaliate, shoot, Wait, Defend",
			"do not require movement",
			"Forced displacement or teleportation removes the roots",
			"roundsRemaining(status.remainingRounds)",
		):
			self.assertIn(term, helper)

	def test_panel_keeps_help_on_the_spell_slot_without_adding_a_row(self):
		self.assertIn("newHorizonsBattleStatus::entangleTooltip(", PANEL)
		self.assertIn("Rect(slotX, slotY, 48, 36), tooltip, tooltip", PANEL)
		self.assertIn("refreshDefendStatus", PANEL)
		self.assertIn("entangleStatus", NATIVE_TEST)


if __name__ == "__main__":
	unittest.main()
