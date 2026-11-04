#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current Estates catalogue, recipient pricing and authored starting references."""
import csv
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'new-horizons:estates.'
ROSTER = (
    ('taxCollector', 'Tax Collector', 'basic'), ('landSurveyor', 'Land Surveyor', 'basic'),
    ('prosperity', 'Prosperity', 'basic'), ('merchantPrince', 'Merchant Prince', 'basic'),
    ('stewardship', 'Stewardship', 'advanced'), ('conscription', 'Conscription', 'advanced'),
    ('architect', 'Architect', 'advanced'), ('mercenaryCompany', 'Mercenary Company', 'advanced'),
    ('banking', 'Banking', 'expert'), ('alchemist', 'Alchemist', 'expert'),
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


class EstatesDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load('config/newHorizonsPerks.json')
        cls.skill = cls.rules['skills']['new-horizons:estates']
        cls.perks = {p['id'].removeprefix(PREFIX): p for p in cls.skill['perks']}
        cls.heroes = load('config/newHorizonsHeroes.json')

    def test_exact_active_roster_tiers_and_schema(self):
        self.assertEqual([(p['id'].removeprefix(PREFIX), p['name'], p['requires'])
                          for p in self.skill['perks']], list(ROSTER))
        self.assertEqual(len(self.perks), 10)
        for perk in self.perks.values():
            self.assertEqual(perk['effect']['status'], 'active')
        Draft4Validator(load('config/schemas/newHorizonsPerks.json')).validate(self.rules)
        Draft4Validator(load('config/schemas/newHorizonsHeroes.json')).validate(self.heroes)

    def test_core_income_is_preserved_and_main_help_is_short(self):
        entities = load('config/newHorizonsSkills.json')['estates']
        self.assertEqual(self.skill['availability'], 'All heroes')
        for rank, gold in (('basic', 125), ('advanced', 250), ('expert', 500)):
            self.assertIn(str(gold), self.skill['ranks'][rank]['description'])
            self.assertEqual(entities[rank]['effects']['dailyGold']['val'], gold)
        self.assertIn('Lord Haart’s existing specialty scales only that core income',
                      self.skill['details'])
        for perk in self.perks.values():
            main = perk['description']
            self.assertLessEqual(len(main.split()), 25)
            self.assertEqual(main.count('.'), 1)
            self.assertNotIn('\n', main)
            self.assertEqual(main, perk['effect']['description'])

    def test_daily_and_weekly_income_holder_boundaries_are_explicit(self):
        expected = {
            'taxCollector': ('100 Gold per day', '500 Gold per holder'),
            'landSurveyor': ('first mine captured by this hero', 'three times',
                             'Merely visiting an already owned mine does not qualify'),
            'prosperity': ('1 Wood and 1 Ore per day', 'Garrisoned owned heroes count',
                           'tavern, pool, dead and non-owned'),
            'merchantPrince': ('visits or garrisons an owned town', 'once per town',
                               'both residents own the perk'),
            'stewardship': ('visiting or garrison hero', '250 additional Gold per day',
                            'Two distinct qualifying residents each contribute'),
            'banking': ('floor(current Gold treasury / 100)', '1,000 Gold per holder',
                        'same pre-income treasury snapshot', 'do not compound interest'),
            'alchemist': ('Mercury, Sulfur, Crystal and Gems',
                          'Preserve independent resource specialties', 'no Wood, Ore or Gold'),
        }
        for name, phrases in expected.items():
            for phrase in phrases:
                self.assertIn(phrase, self.perks[name]['details'])

    def test_prices_use_actual_recipient_aggregate_rounding_and_limited_building_resources(self):
        recruitment = self.perks['conscription']['details']
        for phrase in ('actual recruitment recipient', 'War Machine purchases',
                       'upper army uses its garrison hero', 'without a garrison hero',
                       'even if a visiting hero owns Conscription',
                       'unit price × recruited count × 90 / 100',
                       'Ten units costing 1 Gold each therefore cost 9 Gold',
                       'Leadership checks'):
            self.assertIn(phrase, recruitment)
        building = self.perks['architect']['details']
        for phrase in ('visiting or garrison hero', 'ceil(ordinary cost × 80 / 100)',
                       'Gold, Wood and Ore', 'precious-resource cost unchanged',
                       'Two holders do not double', 'actual payment use the same price'):
            self.assertIn(phrase, building)

    def test_weekly_rogues_preserve_full_garrisons_and_native_collection(self):
        guards = self.perks['mercenaryCompany']['details']
        for phrase in ('At the start of each week', 'current hero level',
                       'add qualifying holders’ contributions', 'existing Rogues',
                       'all seven slots', 'pending reserve', 'rather than overwriting troops',
                       'native mine-garrison transfers and Leadership checks'):
            self.assertIn(phrase, guards)

    def test_retired_ids_are_absent_and_all_forty_one_affected_profiles_resolve(self):
        for retired in ('prospector', 'steward', 'investor', 'resourceBroker',
                        'estateNetwork', 'financier', 'magnate'):
            self.assertNotIn(retired, self.perks)
        profiles = self.heroes['startingSkills']['startingDevelopmentProfiles']
        relevant = {'new-horizons:' + x for x in ('wisdom', 'warcasting', 'learning', 'estates')}
        affected = {hero: p for hero, p in profiles.items()
                    if any(ref['skill'] in relevant for ref in p.get('startingPerks', []))}
        self.assertEqual(len(affected), 41)
        for hero, profile in affected.items():
            for ref in profile['startingPerks']:
                perk = next(p for p in self.rules['skills'][ref['skill']]['perks']
                            if p['id'] == ref['perk'])
                self.assertEqual(perk['requires'], 'basic', hero)
        prosperity = {hero for hero, p in profiles.items() if any(
            ref['perk'] == PREFIX + 'prosperity' for ref in p.get('startingPerks', []))}
        self.assertEqual(prosperity, {'core:saurug', 'core:sephinroth', 'core:rissa',
                                     'core:calid', 'core:grindan'})

    def test_icon_inventory_and_pack_references_match_all_ten(self):
        header = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        bindings = re.findall(r'\{"(new-horizons:estates\.[^"]+)", "([^"]+)"\}', header)
        self.assertEqual(len(bindings), 10)
        self.assertEqual({identity for identity, _ in bindings},
                         {p['id'] for p in self.perks.values()})
        with (ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').open(newline='') as source:
            rows = {row['Item / ID']: row for row in csv.DictReader(source)}
        resources = {entry['resource'] for entry in
                     load('assets/new-horizons/runtime-art-manifest.json')['entries']}
        for identity, stem in bindings:
            perk = self.perks[identity.removeprefix(PREFIX)]
            row = rows[perk['name'] + ' (' + identity + ')']
            self.assertEqual(row['Area'], 'Active perk')
            self.assertEqual(row['Art'], 'Provisional')
            self.assertIn(stem, row['Live binding / source'])
            self.assertIn('requires=' + perk['requires'], row['Live binding / source'])
            for suffix in ('.json', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png'):
                name = 'SPRITES/' + stem + suffix
                self.assertTrue(name in resources, 'Missing packaged resource: ' + name)

    def test_generated_module_matches_current_registry_and_hero_profiles(self):
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks'], self.rules)
        self.assertEqual(module['settings']['heroes']['newHorizons'], self.heroes)
        key = 'new-horizons.economy.mercenaryCompany.pending'
        texts = load('config/newHorizonsEconomyTexts.json')
        text = texts[key]
        self.assertEqual(text.count('%d'), 1)
        self.assertEqual(module['translations'][key], text)
        for retired in ('investor', 'resourceBroker', 'prospector', 'magnate', 'steward'):
            self.assertFalse(any(k.startswith('new-horizons.economy.' + retired + '.')
                                 for k in texts))
        self.assertIn('new-horizons.economy.stewardship.contribution', texts)
        self.assertNotIn('new-horizons.sage.guildSpells', texts)


if __name__ == '__main__':
    unittest.main()
