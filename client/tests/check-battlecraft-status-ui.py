#!/usr/bin/env python3
"""Focused source guard for Battlecraft Wait and Defend readback."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
RULES = (ROOT / "lib/battle/NewHorizonsBattlecraft.cpp").read_text(encoding="utf-8")
UNIT_STATE = (ROOT / "lib/battle/CUnitState.cpp").read_text(encoding="utf-8")
CALLBACK = (ROOT / "lib/battle/CBattleInfoCallback.cpp").read_text(encoding="utf-8")
NATIVE = (ROOT / "test/battle/BattlecraftStatusPresentationTest.cpp").read_text(encoding="utf-8")


class BattlecraftStatusUiTest(unittest.TestCase):
	def test_wait_readback_uses_the_shared_rank_and_armed_one_shot(self):
		wait = PANEL[PANEL.index("currentBattlecraftWaitStatus("):PANEL.index(
			"newHorizonsBattleStatus::StackInfoStatusSnapshot currentStackInfoStatus(")]
		self.assertIn("stack->battlecraftWaitBonusAvailable()", wait)
		self.assertIn("newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)", wait)
		self.assertIn("battleCallback->battleGetFightingHero(ownerSide)", wait)
		self.assertIn("usesNewHorizonsBattleRules(hero)", wait)
		self.assertIn("newHorizonsBattlecraft::rankPercent(newHorizonsBattlecraft::rank(hero))", wait)
		self.assertIn("waitedThisTurn && !battlecraftWaitBonusUsed", UNIT_STATE)
		self.assertIn("newHorizonsBattlecraft::rankPercent(", CALLBACK)

	def test_profile_gate_and_privacy_scoped_defend_readback(self):
		profile = PANEL[PANEL.index("bool usesNewHorizonsBattleRules("):PANEL.index("struct StackStatusEntry")]
		self.assertIn("newHorizonsHeroes::usesRules(rules)", profile)
		self.assertIn('rules["rulesetVersion"].Integer() >= 3', profile)
		defend = PANEL[PANEL.index("newHorizonsBattleStatus::DefendStatus currentDefendStatus("):PANEL.index(
			"std::optional<newHorizonsBattleStatus::BattlecraftWaitStatus> currentBattlecraftWaitStatus(")]
		self.assertIn("battleCallback->battleGetFightingHero(ownerSide)", defend)
		self.assertIn("usesNewHorizonsBattleRules(hero)", defend)
		self.assertIn("newHorizonsBattlecraft::defendReductionPercent(hero)", defend)
		self.assertIn("int defendReductionPercent(const CGHeroInstance * hero)", RULES)
		self.assertNotIn("stack->getMyHero()", defend)

	def test_same_compact_status_slot_preserves_defend_and_soul_badges(self):
		self.assertIn("if(displayedStatus.defend.defending)", PANEL)
		self.assertIn('displayedStatus.defend.bulwark ? "BULWARK" : "DEFEND"', PANEL)
		self.assertIn('Colors::YELLOW, "WAIT"', PANEL)
		self.assertEqual(PANEL.count("Rect(7, 153, 39, 13), tooltip, tooltip"), 2)
		self.assertIn('Colors::YELLOW, "SOUL"', PANEL)
		self.assertIn("result.battlecraftWait = currentBattlecraftWaitStatus(stack, battleCallback)", PANEL)
		self.assertIn("if(current == displayedStatus", PANEL)
		self.assertIn("panel->refreshDefendStatus(stack)", WINDOW)

	def test_help_uses_actual_action_expiry_and_physical_reduction_context(self):
		wait_tooltip = STATUS[STATUS.index("inline std::string battlecraftWaitTooltip("):STATUS.index("inline std::optional<BulwarkStatus>")]
		self.assertIn("next attack or retaliation before the end of this round", wait_tooltip)
		self.assertIn("status.damageBonusPercent", wait_tooltip)
		tooltip = STATUS[STATUS.index("inline std::string defendStatusTooltip("):STATUS.index("inline std::string beneficiarySideName")]
		self.assertIn("until its next normal Creature Activation", tooltip)
		self.assertIn("status.battlecraftReductionPercent", tooltip)
		self.assertIn("shared Physical Damage Reduction cap", tooltip)
		self.assertIn("if(!status.bulwark)", tooltip)
		self.assertIn("status.bulwark", tooltip)
		self.assertNotIn("through the end of the current battle round", tooltip)

	def test_native_coverage_includes_ranked_wait_defend_and_inert_states(self):
		self.assertIn("WaitReadbackShowsRankBonusOnlyWhileArmed", NATIVE)
		self.assertIn("for(int rank = 1; rank <= 3; ++rank)", NATIVE)
		self.assertIn("DefendReadbackComposesBattlecraftEntrenchAndBulwark", NATIVE)
		self.assertIn("OrdinaryDefendHasNoInertBattlecraftClaim", NATIVE)


if __name__ == "__main__":
	unittest.main()
