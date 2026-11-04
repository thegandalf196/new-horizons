#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current Arcane Inspiration catalogue and default hero references."""
import csv
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'new-horizons:warcasting.'
ROSTER = (
    ('spellward', 'Spellward', 'basic'),
    ('arcaneValor', 'Arcane Valor', 'basic'),
    ('spellshock', 'Spellshock', 'basic'),
    ('battleFocus', 'Battle Focus', 'basic'),
    ('arcaneBulwark', 'Arcane Bulwark', 'advanced'),
    ('enchantedWeapons', 'Enchanted Weapons', 'advanced'),
    ('lingeringInspiration', 'Lingering Inspiration', 'advanced'),
    ('arcaneReprisal', 'Arcane Reprisal', 'advanced'),
    ('grandInspiration', 'Grand Inspiration', 'expert'),
    ('arcaneSupremacy', 'Arcane Supremacy', 'expert'),
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


class WarcastingDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load('config/newHorizonsPerks.json')
        cls.skill = cls.rules['skills']['new-horizons:warcasting']
        cls.perks = {p['id'].removeprefix(PREFIX): p
                     for p in cls.skill['perks']}

    def test_exact_ten_active_perks_and_schema(self):
        self.assertEqual([(p['id'].removeprefix(PREFIX), p['name'], p['requires'])
                          for p in self.skill['perks']], list(ROSTER))
        self.assertEqual(len(self.perks), 10)
        for perk in self.perks.values():
            self.assertEqual(perk['effect']['status'], 'active')
        Draft4Validator(load('config/schemas/newHorizonsPerks.json')).validate(self.rules)

    def test_rank_progression_and_short_help_are_current(self):
        entities = load('config/newHorizonsSkills.json')['warcasting']
        for rank, value in (('basic', 5), ('advanced', 10), ('expert', 15)):
            definition = self.skill['ranks'][rank]
            self.assertIn('+' + str(value) + '%', definition['description'])
            self.assertEqual(entities[rank]['description'], definition['description'])
            self.assertIn('start of the next combat round', definition['details'])
        for definition in [self.skill, *self.skill['ranks'].values(),
                           *self.perks.values()]:
            summary = definition['description']
            self.assertLessEqual(len(summary.split()), 25)
            self.assertEqual(summary.count('.'), 1)
            self.assertNotIn('\n', summary)
            if 'effect' in definition:
                self.assertEqual(definition['effect']['description'], summary)

    def test_alternation_and_removed_perks_are_not_current(self):
        for old in ('martialChanneling', 'arcaneChanneling', 'battleMeditation',
                    'enchantedCommand', 'combatCasting', 'tacticalWeaving',
                    'reactiveWeave', 'perfectRhythm', 'masterSynthesis', 'counterspell'):
            self.assertNotIn(old, self.perks)
        current = json.dumps(self.skill)
        for old in ('Spell-to-Order', 'Order-to-Spell', 'alternate spells',
                    'alternating bonuses', 'Counterspell'):
            self.assertNotIn(old, current)

    def test_lifetimes_damage_boundaries_and_native_strike_semantics(self):
        for phrase in ('Refresh the effect without stacking', 'damage over time',
                       'not a hero spell buff', 'grants no additional Hero Action',
                       'Successful automatic hero spells', 'Echo repetitions',
                       'after the triggering spell’s effects', 'later arrivals'):
            self.assertIn(phrase, self.skill['details'])
        expected = {
            'spellward': ('whether or not Arcane Inspiration is active',),
            'arcaneValor': ('+1 Morale', 'ordinary Morale limits and immunities'),
            'spellshock': ('actually damaged', '10%', 'refresh without stacking',
                           'Creature abilities and damage over time are not weakened'),
            'battleFocus': ('15 percentage points', 'retaliations only'),
            'arcaneBulwark': ('10% less physical damage',),
            'enchantedWeapons': ('15% of the target’s Defense',),
            'lingeringInspiration': ('one nonstacking retained charge', 'including Grand Inspiration',
                                    'first accepted attack strike, including a retaliation',
                                    'even while normal Arcane Inspiration remains active',
                                    'rather than an entire multistrike sequence',
                                    'expiry creates no fresh charge',
                                    'never double', 'no other inspiration perk effects'),
            'arcaneReprisal': ('damage remaining after temporary shields', 'magical damage',
                              'A fully absorbed hit reflects zero',
                              'before casualties', 'a lethal hit still qualifies',
                              'cannot trigger itself', 'grants no extra action'),
            'grandInspiration': ('10 percentage points', '+15%, +20% and +25%'),
            'arcaneSupremacy': ('20%', 'Physical Damage Reduction',
                               'Magical Damage Reduction', 'does not ignore Defense',
                               'or improve hero spells'),
        }
        for name, phrases in expected.items():
            for phrase in phrases:
                self.assertIn(phrase, self.perks[name]['details'])

    def test_icons_and_inventory_match_current_names_tiers_and_existing_resources(self):
        header = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        bindings = re.findall(r'\{"(new-horizons:warcasting\.[^"]+)", "([^"]+)"\}', header)
        self.assertEqual(len(bindings), 10)
        self.assertEqual({identity for identity, _ in bindings},
                         {PREFIX + identity for identity in self.perks})
        icons = dict(bindings)
        resources = {e['resource'] for e in
                     load('assets/new-horizons/runtime-art-manifest.json')['entries']}
        with (ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').open(newline='') as source:
            rows = {row['Item / ID']: row for row in csv.DictReader(source)}
        for perk in self.perks.values():
            row = rows[perk['name'] + ' (' + perk['id'] + ')']
            self.assertEqual(row['Area'], 'Active perk')
            self.assertEqual(row['Art'], 'Provisional')
            self.assertIn('requires=' + perk['requires'], row['Live binding / source'])
            self.assertIn(icons[perk['id']], row['Live binding / source'])
            for suffix in ('.json', '_normal.png', '_pressed.png',
                           '_disabled.png', '_highlighted.png'):
                resource = 'SPRITES/' + icons[perk['id']] + suffix
                self.assertTrue(resource in resources, 'Missing resource: ' + resource)

    def test_all_default_hero_perks_resolve_and_halon_has_basic_arcane_valor(self):
        heroes = load('config/newHorizonsHeroes.json')
        profiles = heroes['startingSkills']['startingDevelopmentProfiles']
        refs = {}
        for hero, profile in profiles.items():
            for perk in profile.get('startingPerks', []):
                parent = self.rules['skills'][perk['skill']]
                found = [p for p in parent['perks'] if p['id'] == perk['perk']]
                self.assertEqual(len(found), 1, hero)
                if perk['skill'] == 'new-horizons:warcasting':
                    refs[hero] = perk['perk'].removeprefix(PREFIX)
                    self.assertEqual(found[0]['requires'], 'basic')
        self.assertEqual(refs, {'core:halon': 'arcaneValor', 'core:straker': 'spellward',
                               'core:thorgrim': 'spellward', 'core:ufretin': 'spellward',
                               'core:fafner': 'spellward', 'core:ajit': 'spellward'})

    def test_shipped_registry_skills_and_hero_profiles_match(self):
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks'], self.rules)
        self.assertEqual(module['skills'], load('config/newHorizonsSkills.json'))
        self.assertEqual(module['settings']['heroes']['newHorizons'],
                         load('config/newHorizonsHeroes.json'))


if __name__ == '__main__':
    unittest.main()
