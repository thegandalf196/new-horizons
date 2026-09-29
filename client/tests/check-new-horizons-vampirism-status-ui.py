#!/usr/bin/env python3
"""Source guard for Vampirism status readback and Night Feeder presentation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "client/battle/StackInfoStatusPresentation.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
PERK_ICONS = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(encoding="utf-8")
PERK_BROWSER = (ROOT / "client/windows/NewHorizonsPerkBrowser.cpp").read_text(encoding="utf-8")
HERO_GROWTH = (ROOT / "client/windows/HeroGrowthWindow.cpp").read_text(encoding="utf-8")
PERKS = (ROOT / "config/newHorizonsPerks.json").read_text(encoding="utf-8")


class VampirismStatusUiSourceTest(unittest.TestCase):
    def test_status_readback_matches_the_spell_bound_timed_trigger(self):
        self.assertIn('VAMPIRISM_SPELL_KEY = "new-horizons:vampirism"', STATUS)
        self.assertIn('VAMPIRISM_TRIGGER_KEY = "core:vampirism"', STATUS)
        helper = STATUS[STATUS.index("VampirismStatus vampirismStatus("):STATUS.index("inline std::string shadowGiftBuffTooltip(")]
        self.assertIn("BonusType::COMBAT_EVENT_TRIGGER", helper)
        self.assertIn("BonusSource::SPELL_EFFECT", helper)
        self.assertIn("BonusDuration::N_TURNS", helper)
        self.assertIn("bonus->turnsRemain <= 0", helper)
        self.assertIn("bonus->val <= 0", helper)
        self.assertIn("VAMPIRISM_SPELL_KEY", helper)
        self.assertIn("VAMPIRISM_TRIGGER_KEY", helper)
        self.assertIn("return {bonus->val, bonus->turnsRemain};", helper)

    def test_existing_stack_spell_slot_shows_lifesteal_and_remaining_rounds(self):
        self.assertIn("VampirismStatus vampirism;", STATUS)
        self.assertIn("result.vampirism = newHorizonsBattleStatus::vampirismStatus(*spellBonuses);", PANEL)
        self.assertIn("StackStatusIconKind::VAMPIRISM", PRESENTATION)
        self.assertIn("StackStatusIconKind::VAMPIRISM: return 2", PRESENTATION)
        self.assertIn("formatBasisPoints(displayedStatus.vampirism.lifestealBasisPoints)", PANEL)
        self.assertIn("vampirismTooltip(", PANEL)
        self.assertIn('" of actual attack damage dealt, including retaliation."', STATUS)
        self.assertIn('"\\nRemaining: " + roundsRemaining(status.remainingRounds)', STATUS)
        self.assertIn('AnimationPath::builtin("SpellInt"), effect.getNum() + 1', PANEL)
        self.assertIn("Rect(slotX, slotY, 48, 36), tooltip, tooltip", PANEL)

    def test_night_feeder_uses_its_art_and_existing_perk_help(self):
        self.assertIn('{"new-horizons:shadowMagic.nightFeeder", "NH_perk_night_feeder"}', PERK_ICONS)
        self.assertIn('AnimationPath::builtin(newHorizonsPerkIcon(perk.id))', PERK_BROWSER)
        self.assertIn("newHorizonsPerkHelp::format(&hero, skillId, perk.name", PERK_BROWSER)
        self.assertIn("newHorizonsPerkHelp::format(&hero, skillId, perk.name", HERO_GROWTH)
        self.assertIn("Vampirism returns an additional 15 percentage points of damage as healing.", PERKS)


if __name__ == "__main__":
    unittest.main()
