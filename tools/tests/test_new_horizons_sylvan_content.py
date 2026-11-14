#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Captured Sylvan content and authored-default contracts, independent of native combat proof."""
import copy
from pathlib import Path
import unittest

from jsonschema import Draft4Validator

if __package__:
    from .test_new_horizons_content import load
else:
    from test_new_horizons_content import load

ROOT = Path(__file__).resolve().parents[2]
SKILL = 'new-horizons:sylvanLuck'
EXPECTED = (
    ('elvenPrecision', 'Hunting Pack', 'basic', {'attackDamagePercent': 25}),
    ('forestSFavor', 'Dwarven Hardiness', 'basic', {'maximumNegativeSpellRounds': 1}),
    ('serendipity', 'Disarming Shot', 'basic', {'retaliationsConsumed': 1}),
    ('luckyRecovery', 'Briar Grip', 'basic', {'attackDamageReductionPercent': 20}),
    ('sharedFortune', 'Evergreen Blessings', 'advanced', {'beneficialDurationBonusRounds': 2}),
    ('natureSProvidence', 'Silver Wings', 'advanced', {'enemySpellCostBonus': 2}),
    ('fortunateAim', 'Sacred Grove', 'advanced', {}),
    ('wildChance', 'Kindred Flame', 'advanced', {}),
    ('perfectMoment', 'Perfect Moment', 'expert', {}),
    ('cascadingFortune', 'Ancient Accord', 'expert', {}),
)


class SylvanContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load(ROOT / 'config/newHorizonsPerks.json')
        cls.schema = load(ROOT / 'config/schemas/newHorizonsPerks.json')
        cls.skill = cls.rules['skills'][SKILL]
        cls.perks = {row['id'].split('.')[-1]: row for row in cls.skill['perks']}

    def test_policy_two_and_historical_markers_are_supported(self):
        validator = Draft4Validator(self.schema)
        self.assertEqual(self.rules['sylvanTacticsPolicy'], 2)
        for marker in (None, 1, 2):
            candidate = copy.deepcopy(self.rules)
            if marker is None:
                candidate.pop('sylvanTacticsPolicy')
            else:
                candidate['sylvanTacticsPolicy'] = marker
            validator.validate(candidate)
        for invalid in (None, False, True, 0, -1, 3, 1.0, '2', {}, []):
            candidate = copy.deepcopy(self.rules)
            candidate['sylvanTacticsPolicy'] = invalid
            self.assertTrue(list(validator.iter_errors(candidate)), repr(invalid))

    def test_stable_ids_tiers_and_central_parameters(self):
        self.assertEqual(self.skill['name'], 'Sylvan Tactics')
        actual = [(p['id'].split('.')[-1], p['name'], p['requires'],
                   p['effect'].get('parameters', {})) for p in self.skill['perks']]
        self.assertEqual(actual, list(EXPECTED))
        for perk in self.skill['perks']:
            self.assertEqual(perk['effect']['status'], 'active')
            self.assertEqual(perk['effect']['description'], perk['description'])

    def test_rank_and_perfect_moment_are_unchanged(self):
        for rank, value in (('basic', 1), ('advanced', 2), ('expert', 3)):
            self.assertEqual(self.skill['ranks'][rank]['description'],
                             f'Friendly creatures gain +{value} Initiative.')
        perfect = self.perks['perfectMoment']
        self.assertEqual(perfect['description'], 'Creature Lucky Strikes deal 3.5× damage.')
        self.assertEqual(perfect['effect'], {'status': 'active', 'description': perfect['description']})

    def test_selected_family_starting_perks(self):
        profiles = load(ROOT / 'config/newHorizonsHeroes.json')['startingSkills']['startingPerkProfiles']
        for hero, suffix in (('ivor', 'serendipity'), ('gelu', 'serendipity'),
                             ('ufretin', 'forestSFavor'), ('ryland', 'luckyRecovery'),
                             ('kyrre', 'elvenPrecision')):
            self.assertIn({'skill': SKILL, 'perk': SKILL + '.' + suffix}, profiles['core:' + hero])

    def test_new_numeric_parameters_reject_wrong_types_and_ranges(self):
        schema = self.schema['definitions']['commandAuthorityParameters']
        validator = Draft4Validator(schema)
        for _, _, _, parameters in EXPECTED:
            if not parameters:
                continue
            validator.validate(parameters)
            key = next(iter(parameters))
            for value in (True, -1, '1', None):
                self.assertTrue(list(validator.iter_errors({key: value})), (key, value))
        self.assertTrue(list(validator.iter_errors({'attackDamageReductionPercent': 101})))


if __name__ == '__main__':
    unittest.main()
