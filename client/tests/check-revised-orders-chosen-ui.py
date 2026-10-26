#!/usr/bin/env python3
"""Source contracts for revised Order snapshots and Chosen readback (not rendered QA)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

class RevisedPresentationTest(unittest.TestCase):
    def test_saved_roster_controls_visibility_and_targeting(self):
        display = source("client/battle/NewHorizonsOrderPresentation.h")
        targeting = display.split("inline bool isTargeted(", 1)[1].split("\n}", 1)[0]
        self.assertIn("heroCommands::supportedByRules(*rules, display.command)", display)
        for old in ("PROTECT", "FLANK", "FOCUS_FIRE", "SECOND_WIND"):
            self.assertIn("HeroCommand::" + old, targeting)
        for current in ("FEINT", "VANGUARD"):
            self.assertNotIn("HeroCommand::" + current, targeting)
            self.assertIn('{HeroCommand::' + current + ', "NH_orders_gauntlet_framed"', display)

    def test_active_values_and_expiry_come_from_issued_snapshot(self):
        display = source("client/battle/NewHorizonsOrderPresentation.h")
        helper = display.split("inline std::string revisedSnapshotText(", 1)[1].split("inline std::string revisedActiveHelp(", 1)[0]
        for field in ("feintReductionPercent", "vanguardStatBonus", "vanguardInitiativePercent", "expiresAfterRound - currentRound + 1"):
            self.assertIn(field, helper)
        self.assertNotIn("getFightingHero", helper)
        indicators = source("client/windows/NewHorizonsOrderIndicators.h")
        self.assertIn("battle->battleOrderBenefitAppliesTo(*state, side, stack)", indicators)
        self.assertIn("heroCommands::revisedFeintReductionFor(*state, stack->unitId())", indicators)
        self.assertNotIn("state->revision->contains(stack->unitId())", indicators)
        self.assertIn("heroCommands::activeVanguardBonuses(stack)", indicators)
        self.assertIn("vanguard->roundsRemaining", indicators)
        self.assertLess(indicators.index("heroCommands::activeVanguardBonuses(stack)"), indicators.index("battleGetHeroOrderStates(side)"))
        self.assertIn("battleOrderBenefitAppliesTo(*state, side, stack)", indicators)
        expiry = source("client/windows/NewHorizonsOrderBadgeHelp.h")
        self.assertIn("state.command == HeroCommand::VANGUARD && state.revision", expiry)
        self.assertIn("state.revision->expiresAfterRound", expiry)

    def test_revised_preview_finalizes_only_a_prepared_owned_copy(self):
        display = source("client/battle/NewHorizonsOrderPresentation.h")
        preview = display.split("inline RevisedOrderPreview revisedOrderPreview(", 1)[1].split("inline std::string commandEffects(", 1)[0]
        self.assertIn("callback.battlePrepareHeroOrderState(side, command", preview)
        self.assertIn("heroCommands::finalizeRevisedOrderSnapshot(*prepared, hero->getMagicRules())", preview)
        self.assertIn("Preview without a Lucky Day proc", preview)
        self.assertIn("revisedSnapshotText(*prepared", preview)
        self.assertNotIn("nominalRevisedOrderSnapshot", preview)
        self.assertNotIn("getPrimSkillLevel", preview)
        self.assertNotIn("sendAndApply", preview)

    def test_revised_preview_bypasses_incomplete_legacy_cache_without_repaint_loop(self):
        panel = source("client/battle/BattleHeroActionWindow.cpp")
        legacy = panel.split("void BattleHeroActionWindow::refreshEffects(", 1)[1].split("void BattleHeroActionWindow::refresh()", 1)[0]
        self.assertIn("HeroCommand::FEINT", legacy)
        self.assertIn("HeroCommand::VANGUARD", legacy)
        self.assertIn("continue; // Refreshed from live preparation", legacy)
        live = panel.split("void BattleHeroActionWindow::refresh()", 1)[1].split("void BattleHeroActionWindow::chooseTargetedCommand", 1)[0]
        self.assertIn("revisedOrderPreview(*callback, side, entry.first, hero)", live)
        self.assertIn("effectLabels[i]->getText() != revisedPreview.label", live)
        self.assertIn("flankReadback + revisedPreview.help", live)
        self.assertNotIn("effectsInitialized", live)

    def test_revised_companions_adjust_only_primary_physical_preview_fields(self):
        display = source("client/battle/NewHorizonsOrderPresentation.h")
        effects = display.split("inline std::string commandEffects(", 1)[1]
        # The version gate excludes historical v3 and the key gate excludes
        # unrelated formulas, including any future secondary riders.
        companion = effects.split("if(heroCommands::isRevisedRules(rules)", 1)[1].split("const auto line = effectLabel(key, value);", 1)[0]
        self.assertIn('command == HeroCommand::CHARGE && key == "meleeDamagePercent"', companion)
        self.assertIn('command == HeroCommand::HOLD_THE_LINE && key == "damageReductionPercent"', companion)
        self.assertIn("value += heroCommands::revisedPerkBonus(rules, &hero, command);", companion)
        self.assertEqual(effects.count("revisedPerkBonus("), 1)
        self.assertNotIn("HeroCommand::RIPOSTE", companion)
        self.assertNotIn("HeroCommand::SECOND_WIND", companion)
        self.assertNotIn("+ 5", companion)
        iron_discipline = effects.split("hero.hasActivePerk(newHorizonsIronDiscipline::SKILL", 1)[1]
        self.assertNotIn("revisedPerkBonus(", iron_discipline)
        self.assertLess(effects.index("heroCommands::coefficient(formula, hero"), effects.index("value += heroCommands::revisedPerkBonus"))

    def test_chosen_shows_only_its_actual_timed_abilities(self):
        status = source("client/battle/StackSpellStatusPresentation.h")
        for token in ("BonusSource::SPELL_EFFECT", "BonusDuration::N_TURNS", "bonus->turnsRemain <= 0", "bonus->sid.toString() != newHorizonsTheChosen::SPELL_KEY", "std::min(result.remainingRounds", "BonusType::ADDITIONAL_ATTACK", "BonusType::UNLIMITED_RETALIATIONS", "BonusType::FLYING", "BonusType::JOUSTING", "Only one stack per army"):
            self.assertIn(token, status)
        panel = source("client/battle/StackInfoBasicPanel.cpp")
        self.assertIn("newHorizonsStackSpellPresentation::chosenStatus(*spellBonuses)", panel)
        self.assertIn("std::to_string(chosenEffect.remainingRounds)", panel)
        self.assertIn("newHorizonsStackSpellPresentation::chosenTooltip(", panel)
        self.assertIn("newHorizonsBattleStatus::crusadeTooltip(", panel)

    def test_chosen_preview_uses_shared_nominal_duration(self):
        readout = source("lib/spells/NewHorizonsMagicReadouts.cpp")
        chosen = readout.split("spell->getJsonKey() == newHorizonsTheChosen::SPELL_KEY", 1)[1].split("describeResurrection(result", 1)[0]
        for token in ("newHorizonsTheChosen::rulesFor", "newHorizonsTheChosen::nominalDuration", "spellPowerCoefficientPercent", "PrimarySkill::SPELL_POWER", "exaltedChampionDurationBonus", "chosenSpecialtyDurationBonus", "nominal + exalted + specialty", "Battle-only Extend Spell and Echoed Duration"):
            self.assertIn(token, chosen)
        self.assertNotIn("spellPowerCoefficientBasisPoints", chosen)

if __name__ == "__main__":
    unittest.main()
