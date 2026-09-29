#!/usr/bin/env python3
"""Static regression check for Doom's battle-status presentation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "client/battle/StackInfoStatusPresentation.h").read_text(encoding="utf-8")
HOVER_PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
STACK_WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text(encoding="utf-8")


class DoomStatusUiSourceTest(unittest.TestCase):
    def test_status_reads_authoritative_spell_bonuses_and_duration(self):
        self.assertIn('DOOM_SPELL_KEY = "new-horizons:doom"', STATUS)
        self.assertIn("bonus->type == BonusType::GENERAL_ATTACK_REDUCTION", STATUS)
        self.assertIn("result.damagePenaltyPercent = std::max(result.damagePenaltyPercent, bonus->val)", STATUS)
        self.assertIn("result.remainingRounds = std::max(result.remainingRounds,", STATUS)
        self.assertIn("static_cast<int32_t>(bonus->turnsRemain)", STATUS)
        self.assertIn("bonus->type == BonusType::MORALE", STATUS)
        self.assertIn("result.moralePenalty = std::min(result.moralePenalty, bonus->val)", STATUS)
        self.assertIn("result.doom = newHorizonsBattleStatus::doomStatus(*spellBonuses)", HOVER_PANEL)
        self.assertIn("current == displayedStatus", HOVER_PANEL)

    def test_tooltip_and_badge_show_actual_effect_and_remaining_rounds(self):
        self.assertIn("doomTooltip", HOVER_PANEL)
        self.assertIn("doomTooltip", STACK_WINDOW)
        self.assertIn("doomEffect.damagePenaltyPercent", HOVER_PANEL)
        self.assertIn("doomEffect.damagePenaltyPercent", STACK_WINDOW)
        self.assertIn("Current outgoing damage, including retaliation damage, is reduced by", STATUS)
        self.assertIn("Morale penalty:", STATUS)
        self.assertIn("roundsRemaining(status.remainingRounds)", STATUS)
        self.assertIn("Creature Defense is unchanged", STATUS)

    def test_doom_status_is_prioritized_when_stack_effect_slots_are_crowded(self):
        self.assertIn("StackStatusIconKind::DOOM", PRESENTATION)
        self.assertIn("StackStatusIconKind::DOOM: return 0", PRESENTATION)

        hover_partition = HOVER_PANEL.index("stackStatusDisplayPlan(statusKinds, totalEffectCount)")
        self.assertLess(HOVER_PANEL.index("statusIconKind(effect)"), hover_partition)
        self.assertIn("return newHorizonsBattleStatus::StackStatusIconKind::DOOM", HOVER_PANEL)

        priority_partition = STACK_WINDOW.index("const auto prioritizedEnd = std::stable_partition(spells.begin(), spells.end()")
        time_stop_partition = STACK_WINDOW.index("const auto timeStopEnd = std::stable_partition", priority_partition)
        doom_partition = STACK_WINDOW.index("std::stable_partition(timeStopEnd, prioritizedEnd", time_stop_partition)
        spell_loop = STACK_WINDOW.index("for(SpellID effect : spells)", doom_partition)
        self.assertLess(priority_partition, time_stop_partition)
        self.assertLess(time_stop_partition, doom_partition)
        self.assertLess(doom_partition, spell_loop)
        self.assertIn("isDoom(spellKey)", STACK_WINDOW[priority_partition:time_stop_partition])
        self.assertIn("isDoom(effect.toSpell()->getJsonKey())", STACK_WINDOW[doom_partition:spell_loop])


if __name__ == "__main__":
    unittest.main()
