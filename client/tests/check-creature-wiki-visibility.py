#!/usr/bin/env python3
"""Source/data guards for reference visibility; not native or rendered proof."""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load_jsonc(path):
    text = (ROOT / path).read_text()
    text = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    return json.loads(text)


class CreatureWikiVisibilityContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.common = (ROOT / 'client/windows/wiki/WikiCommon.cpp').read_text()
        cls.rule = cls.common[cls.common.index('bool isWikiCreatureVisible'):cls.common.index('// WikiClickable')]
        cls.window = (ROOT / 'client/windows/CCreatureWindow.cpp').read_text()
        cls.wiki = (ROOT / 'client/windows/wiki/WikiWindow.cpp').read_text()
        cls.mod = (ROOT / 'client/windows/wiki/WikiModContent.cpp').read_text()
        cls.special = load_jsonc('config/creatures/special.json')

    def test_ordinary_and_wraith_membership_retained_before_special_restrictions(self):
        self.assertIn('if(!creature->special || creature->warMachine.hasValue())', self.rule)
        self.assertLess(self.rule.index('!creature->special'), self.rule.index('creature->getLevel()'))
        self.assertNotIn('new-horizons:ghost', self.rule)
        self.assertNotIn('core:wraith', self.rule)

    def test_ghost_and_living_armor_use_authored_positive_tier_models(self):
        for name, level in [('ghost', 3), ('livingArmor', 1)]:
            row = load_jsonc('Mods/new-horizons/Content/config/creatures/' + name + '.json')[name]
            self.assertTrue(row['special'])
            self.assertTrue(row['excludeFromRandomization'])
            self.assertEqual(row['level'], level)
            self.assertTrue(row['graphics']['animation'])
        self.assertIn('creature->getLevel() <= 0 || creature->animDefName.empty()', self.rule)

    def test_registered_war_machine_exception_uses_existing_reciprocal_metadata(self):
        registration = (ROOT / 'lib/entities/artifact/CArtHandler.cpp').read_text()
        self.assertIn('LIBRARY->creh->objects.at(id)->warMachine = art->id', registration)
        self.assertIn('creature->warMachine.hasValue()', self.rule)
        self.assertNotIn('warMachineCreatures', self.wiki)
        for name in ('catapult', 'ballista', 'firstAidTent', 'ammoCart'):
            self.assertTrue(self.special[name]['special'])
            self.assertTrue(self.special[name]['graphics']['animation'])

    def test_internal_and_unused_specials_remain_excluded(self):
        self.assertEqual(self.special['arrowTower']['level'], 0)
        self.assertTrue(self.special['arrowTower']['graphics']['animation'])
        for name in ('unused122', 'unused124', 'unused126', 'unused128'):
            self.assertTrue(self.special[name]['disabled'])
            self.assertEqual(self.special[name].get('level', 0), 0)
        self.assertIn('if(!creature)', self.rule)
        self.assertNotIn('existsResource', self.rule)

    def test_special_commanders_excluded_without_removing_ordinary_commander_types(self):
        classes = load_jsonc('config/heroClasses.json')
        self.assertIn('zealot', {row['commander'] for row in classes.values()})
        self.assertIn('grandElf', {row['commander'] for row in classes.values()})
        self.assertIn('std::none_of(classes.begin(), classes.end()', self.rule)
        self.assertIn('heroClass && heroClass->commander == creature->getId()', self.rule)
        self.assertLess(self.rule.index('return true;'), self.rule.index('std::none_of'))

    def test_popup_button_portrait_and_shortcut_share_registry_eligibility(self):
        self.assertIn('&& isWikiCreatureVisible(parent->info->creature)', self.window)
        self.assertIn('if(!commander && isWikiCreatureVisible(cre))', self.window)
        self.assertIn('!info->commander && isWikiCreatureVisible(info->creature)', self.window)
        self.assertNotIn('!parent->info->creature->special', self.window)

    def test_list_exact_key_navigation_and_mod_indexes_remain_coherent(self):
        self.assertIn('if(isWikiCreatureVisible(creature.get()))', self.wiki)
        self.assertIn('currentDisplayedEntries[i].identifier == key.entryName', self.wiki)
        self.assertIn('if(isWikiCreatureVisible(c.get()))', self.mod)
        self.assertIn('if(isWikiCreatureVisible(obj.get()) && isEntityOfMod', self.mod)


if __name__ == '__main__':
    unittest.main()
