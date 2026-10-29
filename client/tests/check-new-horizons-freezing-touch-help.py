#!/usr/bin/env python3
"""Static binding guard; not compiled or rendered Freezing Touch acceptance."""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELP = (ROOT / "client/windows/NewHorizonsCreatureAbilityHelp.h").read_text()
BINDINGS = (ROOT / "client/windows/NewHorizonsCreatureAbilityHelp.cpp").read_text()
WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text()
WIKI = (ROOT / "client/windows/wiki/WikiCreatureContent.cpp").read_text()
HEADER = (ROOT / "client/windows/CCreatureWindow.h").read_text()
TEXTS = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text())


class FreezingTouchHelpBindingTest(unittest.TestCase):
    def test_innate_identity_not_current_attack_eligibility(self):
        self.assertIn('creatureKey != "core:iceElemental" || !capturedRules', HELP)
        self.assertNotIn('isFreezingTouchAttacker', HELP)
        self.assertNotIn('isFrozen', HELP)
        self.assertNotIn('isTimeStopped', HELP)

    def test_percentages_come_from_captured_rules(self):
        self.assertIn('newHorizonsFrozen::chancePercent(*capturedRules)', HELP)
        self.assertIn('newHorizonsFrozen::shatterBonusPercent(*capturedRules)', HELP)
        self.assertIn('if(chance <= 0)', HELP)
        for suffix in ('compact', 'help'):
            text = TEXTS['new-horizons.creature.freezingTouch.' + suffix]
            self.assertIn('%chance%', text)
            self.assertIn('%bonus%', text)
            self.assertNotIn('20%', text)
            self.assertNotIn('25%', text)

    def test_full_help_and_existing_row_geometry(self):
        self.assertIn('std::string tooltip;', HEADER)
        self.assertIn('Rect(position.x + 60, position.y, 137, 50)', WINDOW)
        self.assertIn('Rect(position.x, position.y, 197, 50), bi.tooltip, bi.tooltip', WINDOW)
        self.assertIn('ability.description = help.compact', WINDOW)
        self.assertIn('ability.tooltip = help.details', WINDOW)
        text = TEXTS['new-horizons.creature.freezingTouch.help']
        for phrase in ('melee attack or retaliation', 'once after damage', 'physical Frozen',
                       'next normal Creature Activation', 'without retaliation',
                       'Direct magical damage', 'Cure clears', 'Dispel does not',
                       'per recipient per round', 'even after cleansing'):
            self.assertIn(phrase, text)

    def test_world_and_battle_contexts_without_global_defaults(self):
        self.assertIn('abilityRules = &battleContext->getMagicRules()', WINDOW)
        self.assertIn('abilityRules = &info->stackNode->cb->getMagicRules()', WINDOW)
        self.assertIn('if(CPlayerInterface::battleInt)', WIKI)
        self.assertIn('abilityRules = &battleCallback->getBattle()->getMagicRules()', WIKI)
        self.assertIn('else if(callback)', WIKI)
        self.assertIn('AbilityRow{help.details, help.iconPath.empty() ? ImagePath{} : ImagePath::builtin(help.iconPath)}', WIKI)
        for source in (HELP, WIKI[WIKI.index("// ── 7. Abilities"):WIKI.index("auto bonusList =")]):
            self.assertNotIn('engineSettings()', source)
            self.assertNotIn('config/newHorizonsMagic', source)


class SoulHarvestHelpBindingTest(unittest.TestCase):
    def test_intrinsic_identity_and_growth_constant(self):
        self.assertIn('creatureKey != "new-horizons:ghost"', HELP)
        self.assertIn('newHorizonsSoulHarvest::GHOSTS_PER_CREATURE_KILLED', HELP)
        self.assertNotIn('GHOSTS_PER_DESTRUCTION', HELP)
        for forbidden in ('hasGhostUpgrade', 'HeroTypeID', 'qualifies(', 'suppressionLevel('):
            self.assertNotIn(forbidden, HELP)

    def test_same_provider_and_selected_icon_in_both_consumers(self):
        for source in (WINDOW, WIKI):
            self.assertIn('newHorizonsCreatureAbilityHelp::translatedInnateAbilities(', source)
            self.assertIn('ImagePath::builtin(help.iconPath)', source)
        self.assertIn('newHorizons/heroSpecialties/VidominaSoulHarvest/icon-44.png', HELP)
        cmake = (ROOT / 'client/CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('windows/NewHorizonsCreatureAbilityHelp.cpp'), 1)
        for suffix in ('compact', 'help'):
            self.assertIn('new-horizons.creature.soulHarvest.' + suffix, BINDINGS)

    def test_truthful_help_and_shipped_translation_mirror(self):
        translations = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())['translations']
        for suffix in ('compact', 'help'):
            key = 'new-horizons.creature.soulHarvest.' + suffix
            self.assertIn('%count%', TEXTS[key])
            self.assertEqual(translations[key], TEXTS[key])
            self.assertNotIn('per stack destroyed', TEXTS[key])
        for phrase in ('innate', 'enemy creature killed', 'physical attack or retaliation',
                       'Partial casualties count', 'only for this battle',
                       'Clones and summoned', 'Suppressed creature abilities',
                       'temporary battle forms', 'resurrected', 'never join the army'):
            self.assertIn(phrase, TEXTS['new-horizons.creature.soulHarvest.help'])


if __name__ == '__main__':
    unittest.main()
