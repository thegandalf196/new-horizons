#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current Learning roster, captured reroll policy and specialty contracts."""
import copy
import csv
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'new-horizons:learning.'
ROSTER = (
    ('scholar', 'Scholar', 'basic'), ('historian', 'Historian', 'basic'),
    ('mentor', 'Mentor', 'basic'), ('eagleEye', 'Eagle Eye', 'basic'),
    ('seasonedAdventurer', 'Seasoned Adventurer', 'advanced'),
    ('connoisseur', 'Connoisseur', 'advanced'),
    ('fieldStudy', 'Field Study', 'advanced'), ('duelLessons', 'Duel Lessons', 'advanced'),
    ('prodigy', 'Prodigy', 'expert'), ('philosopher', 'Philosopher', 'expert'),
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


class LearningDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load('config/newHorizonsPerks.json')
        cls.skill = cls.rules['skills']['new-horizons:learning']
        cls.perks = {p['id'].removeprefix(PREFIX): p for p in cls.skill['perks']}
        cls.heroes = load('config/newHorizonsHeroes.json')

    def test_exact_roster_tiers_active_status_and_schema(self):
        self.assertEqual([(p['id'].removeprefix(PREFIX), p['name'], p['requires'])
                          for p in self.skill['perks']], list(ROSTER))
        self.assertEqual(len(self.perks), 10)
        for perk in self.perks.values():
            self.assertEqual(perk['effect']['status'], 'active')
        Draft4Validator(load('config/schemas/newHorizonsPerks.json')).validate(self.rules)

    def test_all_hero_rank_xp_is_preserved_and_main_help_is_short(self):
        self.assertEqual(self.skill['availability'], 'All heroes')
        entities = load('config/newHorizonsSkills.json')['learning']
        for rank, xp in (('basic', 10), ('advanced', 20), ('expert', 30)):
            self.assertIn(str(xp) + '%', self.skill['ranks'][rank]['description'])
            self.assertEqual(entities[rank]['effects']['experience']['val'], xp)
        for perk in self.perks.values():
            summary = perk['description']
            self.assertLessEqual(len(summary.split()), 25)
            self.assertEqual(summary.count('.'), 1)
            self.assertNotIn('\n', summary)
            self.assertEqual(perk['effect']['description'], summary)
            self.assertTrue(perk['details'])

    def test_removed_perks_and_old_policy_are_not_current(self):
        for identity in ('academicStudy', 'quickStudy', 'archivist', 'sage', 'masterTeacher'):
            self.assertNotIn(identity, self.perks)
        self.assertNotIn('quickStudyPolicy', self.rules)
        wisdom = self.rules['skills']['new-horizons:wisdom']['perks']
        sage = next(p for p in wisdom if p['id'] == 'new-horizons:wisdom.sage')
        self.assertEqual(sage['name'], 'Sage')
        self.assertIn('50', sage['description'])

    def test_connoisseur_captured_policy_rejects_old_interval_and_unknown_fields(self):
        validator = Draft4Validator(load('config/schemas/newHorizonsPerks.json'))
        self.assertEqual(self.rules['connoisseurPolicy'], {'version': 1})
        for invalid in (None, {}, {'version': 2}, {'version': True},
                        {'version': 1, 'interval': 3}, {'version': 1, 'extra': 1}):
            rules = {**self.rules, 'connoisseurPolicy': invalid}
            self.assertTrue(list(validator.iter_errors(rules)))
        self.assertTrue(list(validator.iter_errors(
            {**self.rules, 'quickStudyPolicy': {'version': 1, 'interval': 3}})))

    def test_specialty_xp_coefficients_remain_without_extra_spell_fields(self):
        schema = load('config/schemas/newHorizonsHeroes.json')
        validator = Draft4Validator(schema)
        validator.validate(self.heroes)
        for table, identity, retired in (
            ('additionalSpecialties', 'graveArchivist', 'additionalEagleEyeSpells'),
            ('learningExchangeSpecialties', 'ecumenicalScholar', 'additionalSpellsTaught'),
            ('learningExchangeSpecialties', 'occultScholar', 'additionalSpellsReceived'),
        ):
            self.assertEqual(self.heroes[table][identity],
                             {'learningPercentPerLevel': 5, 'learningMaximumPercent': 100})
            invalid = copy.deepcopy(self.heroes)
            invalid[table][identity][retired] = 1
            self.assertTrue(list(validator.iter_errors(invalid)))

    def test_precision_and_source_boundaries_are_in_details(self):
        expected = {
            'scholar': ('each teaches the other every permanently known spell',
                        'no one-spell limit or weekly hero-pair limit'),
            'historian': ('primary reward is Experience', '50%'),
            'mentor': ('first eligible lower-level allied hero', '250 ×',
                       'recipient’s ordinary Experience modifiers', 'Oris'),
            'eagleEye': ('every distinct eligible unknown spell', 'highest spell level first',
                         'no separate Level 1–3 cap or one-spell count limit'),
            'seasonedAdventurer': ('permanent primary Attribute rewards',
                                  'Temporary Attribute bonuses, resources and Experience',
                                  'floor(amount × 125 / 100)'),
            'connoisseur': ('one offered secondary Skill once', 'all perk choices remain unchanged',
                            'every normal level-up', 'does not reroll the whole offer'),
            'fieldStudy': ('All combat Experience', 'no Army Value comparison'),
            'duelLessons': ('defeated hero’s total accumulated Experience', 'floor(10%',
                            'before defeat cleanup or hero removal',
                            '20,000 total Experience grants 2,000 additional Experience',
                            'multipliers do not multiply it again'),
            'prodigy': ('three normal level-ups once', 'native level cap',
                        'normal primary Attribute gains', 'Thane',
                        'without applying Experience multipliers'),
            'philosopher': ('25 percentage points', '+35%, +45% and +55%',
                            'does not boost Mentor awards'),
        }
        for identity, phrases in expected.items():
            for phrase in phrases:
                self.assertIn(phrase, self.perks[identity]['details'])

    def test_all_twenty_eight_affected_starting_profiles_resolve(self):
        relevant = {'new-horizons:' + skill for skill in ('wisdom', 'warcasting', 'learning')}
        profiles = self.heroes['startingSkills']['startingDevelopmentProfiles']
        affected = {hero: profile for hero, profile in profiles.items()
                    if any(p['skill'] in relevant for p in profile.get('startingPerks', []))}
        self.assertEqual(len(affected), 28)
        for hero, profile in affected.items():
            for reference in profile['startingPerks']:
                perk = next(p for p in self.rules['skills'][reference['skill']]['perks']
                            if p['id'] == reference['perk'])
                self.assertEqual(perk['requires'], 'basic', hero)
        self.assertEqual(profiles['core:halon']['startingPerks'][0]['perk'],
                         'new-horizons:warcasting.arcaneValor')

    def test_current_icons_inventory_and_pack_resource_references_agree(self):
        source = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        bindings = re.findall(r'\{"(new-horizons:learning\.[^"]+)", "([^"]+)"\}', source)
        self.assertEqual(len(bindings), 10)
        self.assertEqual({identity for identity, _ in bindings},
                         {p['id'] for p in self.perks.values()})
        resources = {e['resource'] for e in
                     load('assets/new-horizons/runtime-art-manifest.json')['entries']}
        with (ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').open(newline='') as inventory:
            rows = {row['Item / ID']: row for row in csv.DictReader(inventory)}
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

    def test_shipped_module_matches_registry_and_hero_contracts(self):
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks'], self.rules)
        self.assertEqual(module['settings']['heroes']['newHorizons'], self.heroes)


if __name__ == '__main__':
    unittest.main()
