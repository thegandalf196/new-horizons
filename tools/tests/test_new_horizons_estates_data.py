#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current Estates content, captured compatibility and authored starting references."""
import csv
import json
from pathlib import Path
import re
import unittest
from jsonschema import Draft4Validator
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'new-horizons:estates.'
ROSTER = (
    ('taxCollector', 'Tax Collector', 'basic'), ('supervisor', 'Supervisor', 'basic'),
    ('mercenaryCompany', 'Mercenary Company', 'basic'), ('merchantPrince', 'Merchant Prince', 'basic'),
    ('stewardship', 'Stewardship', 'advanced'), ('conscription', 'Conscription', 'advanced'),
    ('architect', 'Architect', 'advanced'), ('hoarder', 'Hoarder', 'advanced'),
    ('decentralization', 'Decentralization', 'expert'), ('alchemist', 'Alchemist', 'expert'),
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
        self.assertEqual(self.rules['estatesPolicyVersion'], 1)
        self.assertEqual([(p['id'].removeprefix(PREFIX), p['name'], p['requires'])
                          for p in self.skill['perks']], list(ROSTER))
        self.assertEqual(len(self.perks), 10)
        self.assertTrue(all(p['effect']['status'] == 'active' for p in self.perks.values()))
        registry = Registry().with_resources((p.as_uri(), Resource.from_contents(load(p)))
                                            for p in (ROOT / 'config/schemas').glob('newHorizons*.json'))
        for name, rules in (('newHorizonsPerks', self.rules), ('newHorizonsHeroes', self.heroes)):
            path = ROOT / 'config/schemas' / (name + '.json')
            schema = load(path)
            schema['id'] = path.as_uri()
            Draft4Validator(schema, registry=registry).validate(rules)

    def test_core_income_availability_and_short_help_are_preserved(self):
        entities = load('config/newHorizonsSkills.json')['estates']
        self.assertEqual(self.skill['availability'], 'All heroes')
        self.assertEqual(self.rules['maxPerksPerSkill'], 3)
        for rank, gold in (('basic', 125), ('advanced', 250), ('expert', 500)):
            self.assertIn(str(gold), self.skill['ranks'][rank]['description'])
            self.assertEqual(entities[rank]['effects']['dailyGold']['val'], gold)
        self.assertIn('specialty scales only this core income', self.skill['details'])
        for perk in self.perks.values():
            self.assertLessEqual(len(perk['description'].split()), 25)
            self.assertNotIn('\n', perk['description'])
            self.assertEqual(perk['description'], perk['effect']['description'])

    def test_personal_and_nonstacking_kingdom_boundaries_are_authored(self):
        expected = {
            'taxCollector': ('50%', 'rounded down separately', 'only chosen Gold', 'refunds'),
            'supervisor': ('Sawmills and Ore Pits', 'one extra', 'do not stack', 'scenario modifiers'),
            'merchantPrince': ('two additional Marketplaces', 'regardless', 'do not stack', 'fixed market rates'),
            'stewardship': ('remotely', '1,000', 'one town', 'no immediate income', 'Capture'),
            'hoarder': ('first successful eligible', 'rounded up', 'normal prices', 'no Gold cost'),
            'decentralization': ('two Capitols instead of one', 'do not stack', 'does not destroy'),
            'alchemist': ('weekly production', '3 Mercury', 'do not stack', 'no daily output'),
        }
        for key, phrases in expected.items():
            for phrase in phrases:
                self.assertIn(phrase.casefold(), self.perks[key]['details'].casefold(), (key, phrase))

    def test_prices_explain_shared_carry_and_physical_building_presence(self):
        for phrase in ('direct recruitment', 'seven resources', 'whole kingdom', 'together or separately',
                       '68 then 67', '675', 'failed purchases', 'cancellations'):
            self.assertIn(phrase, self.perks['conscription']['details'])
        for phrase in ('physically', 'Gold, Wood and Ore', 'rounding', 'Precious resources',
                       'do not stack', 'remote Steward', 'daily construction'):
            self.assertIn(phrase, self.perks['architect']['details'])

    def test_mine_defenders_use_highest_level_cap_and_are_never_collected(self):
        guards = self.perks['mercenaryCompany']
        self.assertEqual(guards['requires'], 'basic')
        for phrase in ('highest', 'three times', 'do not stack', 'ordinary battles',
                       'cannot be collected, transferred or sold', 'Capture removes', 'no Rogues'):
            self.assertIn(phrase, guards['details'])

    def test_all_registered_default_references_resolve_including_eight_supervisors(self):
        for identity in ('landSurveyor', 'prosperity', 'banking'):
            self.assertNotIn(identity, self.perks)
        starts = self.heroes['startingSkills']
        self.assertEqual(len(starts['startingPerkProfiles']), 163)
        expected = {'core:jenova', 'core:rissa', 'core:calid', 'core:sephinroth',
                    'core:saurug', 'core:grindan', 'core:mutare', 'core:mutareDrake'}
        actual = set()
        for hero, references in starts['startingPerkProfiles'].items():
            for ref in references:
                row = next(p for p in self.rules['skills'][ref['skill']]['perks'] if p['id'] == ref['perk'])
                self.assertEqual(row['requires'], 'basic', hero)
                if ref['perk'] == PREFIX + 'supervisor':
                    actual.add(hero)
        self.assertEqual(actual, expected)
        for hero in expected:
            self.assertIn({'skill': 'new-horizons:estates', 'perk': PREFIX + 'supervisor'},
                          starts['startingDevelopmentProfiles'][hero]['startingPerks'])

    def test_current_icons_and_inventory_resolve_while_legacy_aliases_remain(self):
        header = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        bindings = dict(re.findall(r'\{"(new-horizons:estates\.[^"]+)", "([^"]+)"\}', header))
        self.assertTrue({p['id'] for p in self.perks.values()} <= set(bindings))
        self.assertTrue({PREFIX + name for name in ('landSurveyor', 'prosperity', 'banking')} <= set(bindings))
        with (ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').open(newline='') as source:
            rows = {row['Item / ID']: row for row in csv.DictReader(source)}
        resources = {entry['resource'] for entry in load('assets/new-horizons/runtime-art-manifest.json')['entries']}
        for perk in self.perks.values():
            stem = bindings[perk['id']]
            row = rows[perk['name'] + ' (' + perk['id'] + ')']
            self.assertEqual(row['Area'], 'Active perk')
            self.assertIn(stem, row['Live binding / source'])
            self.assertIn('requires=' + perk['requires'], row['Live binding / source'])
            for suffix in ('.json', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png'):
                self.assertIn('SPRITES/' + stem + suffix, resources)

    def test_generated_module_captures_current_rules_and_resource_help(self):
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks'], self.rules)
        self.assertEqual(module['settings']['heroes']['newHorizons'], self.heroes)
        for key, text in load('config/newHorizonsEconomyTexts.json').items():
            self.assertEqual(module['translations'][key], text)
        self.assertEqual(module['translations']['new-horizons.economy.taxCollector.bonus'], 'Tax Collector adds:')


if __name__ == '__main__':
    unittest.main()
