#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current Wisdom roster, help, starting references and shipped catalogue."""
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'new-horizons:wisdom.'
ROSTER = (
    ('intelligence', 'Intelligence', 'basic'),
    ('mysticism', 'Mysticism', 'basic'),
    ('spellFamiliarity', 'Spell Familiarity', 'basic'),
    ('arcaneMomentum', 'Arcane Momentum', 'basic'),
    ('spellScholar', 'Spell Scholar', 'advanced'),
    ('manaFasting', 'Mana Fasting', 'advanced'),
    ('manaConservation', 'Mana Conservation', 'advanced'),
    ('arcaneInsight', 'Arcane Insight', 'advanced'),
    ('archmage', 'Archmage', 'expert'),
    ('sage', 'Sage', 'expert'),
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


class WisdomDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load('config/newHorizonsPerks.json')
        cls.wisdom = cls.rules['skills']['new-horizons:wisdom']
        cls.perks = {p['id'].removeprefix(PREFIX): p
                     for p in cls.wisdom['perks']}

    def test_exact_ten_perks_and_tiers_validate(self):
        actual = [(p['id'].removeprefix(PREFIX), p['name'], p['requires'])
                  for p in self.wisdom['perks']]
        self.assertEqual(actual, list(ROSTER))
        self.assertEqual(len(self.perks), 10)
        Draft4Validator(load('config/schemas/newHorizonsPerks.json')).validate(self.rules)
        for perk in self.perks.values():
            self.assertEqual(perk['effect']['status'], 'active')

    def test_removed_perks_are_absent_from_current_catalogue(self):
        for old in ('arcaneMemory', 'preparedCaster', 'meditation',
                    'deepKnowledge', 'arcaneReservoir'):
            self.assertNotIn(old, self.perks)
        self.assertNotIn('reveal', self.perks['sage']['details'].lower())
        self.assertNotIn('costs 3', self.perks['archmage']['details'])
        learning = self.rules['skills']['new-horizons:learning']['perks']
        self.assertFalse(any(p['id'] == 'new-horizons:learning.sage' for p in learning))
        self.assertTrue(any(p['id'] == 'new-horizons:learning.prodigy' for p in learning))

    def test_main_help_is_one_short_sentence_with_precise_details(self):
        for perk in self.perks.values():
            with self.subTest(perk=perk['id']):
                text = perk['description']
                self.assertLessEqual(len(text.split()), 25)
                self.assertEqual(text.count('.'), 1)
                self.assertNotIn('\n', text)
                self.assertEqual(perk['effect']['description'], text)
                self.assertTrue(perk['details'])
        expected = {
            'intelligence': ('floor(1.30 × effective Knowledge)',),
            'spellFamiliarity': ('Wisdom-discounted Mana cost', '5%; round up, minimum 1',
                                 'Count each spell independently', 'start of combat'),
            'arcaneMomentum': ('subsequent casts', 'at most once per round',
                               'without a hero spell cast resets', '0% on the first cast'),
            'spellScholar': ('Adventure spell purchases', '75%', 'zero costs remain zero',
                             'Normal purchase and learning requirements'),
            'manaFasting': ('After victory', 'no completed hero spell casts',
                            'floor(10%', 'capped at maximum'),
            'manaConservation': ('actually spent', 'floor(20%', '20 Mana', 'maximum'),
            'arcaneInsight': ('each distinct combat spell', '25%', 'reset at the start'),
            'archmage': ('Every Level 4 or Level 5', '20%', '10 percentage points'),
            'sage': ('effective Knowledge once', 'Base Knowledge does not change'),
        }
        for name, phrases in expected.items():
            for phrase in phrases:
                self.assertIn(phrase, self.perks[name]['details'])

    def test_all_current_perks_have_exactly_one_existing_resource_binding(self):
        source = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        bindings = re.findall(r'\{"(new-horizons:wisdom\.[^"]+)", "([^"]+)"\}', source)
        self.assertEqual(len(bindings), 10)
        self.assertEqual({identity for identity, _ in bindings},
                         {PREFIX + identity for identity in self.perks})
        resources = {entry['resource'] for entry in
                     load('assets/new-horizons/runtime-art-manifest.json')['entries']}
        for _, resource in bindings:
            for suffix in ('.json', '_normal.png', '_pressed.png',
                           '_disabled.png', '_highlighted.png'):
                self.assertTrue('SPRITES/' + resource + suffix in resources,
                                'Missing packaged resource: ' + resource + suffix)

    def test_default_starting_wisdom_perks_resolve_to_basic_catalogue(self):
        heroes = load('config/newHorizonsHeroes.json')

        def visit(value):
            if isinstance(value, dict):
                for perk in value.get('startingPerks', []):
                    if perk['skill'] == 'new-horizons:wisdom':
                        identity = perk['perk'].removeprefix(PREFIX)
                        self.assertIn(identity, self.perks)
                        self.assertEqual(self.perks[identity]['requires'], 'basic')
                for child in value.values():
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)

        visit(heroes)

    def test_shipped_module_has_same_wisdom_catalogue(self):
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks']
                         ['skills']['new-horizons:wisdom'], self.wisdom)


if __name__ == '__main__':
    unittest.main()
