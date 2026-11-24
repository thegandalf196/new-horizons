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
    ('elvenPrecision', 'Hunting Pack', 'basic', {'attackDamagePercent': 15}),
    ('forestSFavor', 'Fleetfoot', 'basic', {}),
    ('serendipity', 'Patient Hunter', 'basic', {}),
    ('luckyRecovery', 'Dappled Cover', 'basic', {}),
    ('sharedFortune', 'Evergreen Blessings', 'advanced', {'beneficialDurationBonusRounds': 2}),
    ('forestCover', 'Forest Cover', 'advanced', {}),
    ('fortunateAim', 'Disruptive Strike', 'advanced', {}),
    ('startle', 'Startle', 'advanced', {}),
    ('perfectMoment', 'Perfect Moment', 'expert', {}),
    ('apexPredator', 'Apex Predator', 'expert', {}),
)


class SylvanContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load(ROOT / 'config/newHorizonsPerks.json')
        cls.schema = load(ROOT / 'config/schemas/newHorizonsPerks.json')
        cls.skill = cls.rules['skills'][SKILL]
        cls.perks = {row['id'].split('.')[-1]: row for row in cls.skill['perks']}

    def test_policy_five_and_historical_markers_are_supported(self):
        validator = Draft4Validator(self.schema)
        self.assertEqual(self.rules['sylvanTacticsPolicy'], 5)
        for marker in (None, 1, 2, 3, 4, 5):
            candidate = copy.deepcopy(self.rules)
            if marker is None:
                candidate.pop('sylvanTacticsPolicy')
            else:
                candidate['sylvanTacticsPolicy'] = marker
            validator.validate(candidate)
        for invalid in (None, False, True, 0, -1, 6, 1.0, 5.0, '5', {}, []):
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
            self.assertTrue(self.skill['ranks'][rank]['description'].startswith(
                f'Friendly creatures gain +{value} Initiative. '))
            self.assertIn('Concealed', self.skill['ranks'][rank]['description'])
        perfect = self.perks['perfectMoment']
        self.assertEqual(perfect['description'], 'Creature Lucky Strikes deal 3.5× damage.')
        self.assertEqual(perfect['effect'], {'status': 'active', 'description': perfect['description']})

    def test_selected_stable_starting_perks(self):
        profiles = load(ROOT / 'config/newHorizonsHeroes.json')['startingSkills']['startingPerkProfiles']
        expected = {
            'ivor': ('archery', 'targetCaller'),
            'gelu': ('archery', 'skirmisher'),
            'ufretin': ('warcasting', 'battleFocus'),
            'ryland': ('diplomacy', 'negotiator'),
            'kyrre': ('logistics', 'pathfinding'),
        }
        for hero, (skill, perk) in expected.items():
            self.assertEqual(profiles['core:' + hero], [
                {'skill': 'new-horizons:' + skill, 'perk': 'new-horizons:' + skill + '.' + perk}])

    def test_current_and_historical_parameters_reject_wrong_types_and_ranges(self):
        schema = self.schema['definitions']['commandAuthorityParameters']
        validator = Draft4Validator(schema)
        # Policy 2 captures retain their retired parameter payloads. Their schema
        # coverage must not disappear when policy 3 no longer authors those keys.
        parameters_by_slot = (
            {'attackDamagePercent': 25}, {'maximumNegativeSpellRounds': 1},
            {'retaliationsConsumed': 1}, {'attackDamageReductionPercent': 20},
            {'beneficialDurationBonusRounds': 2}, {'enemySpellCostBonus': 2},
        )
        for parameters in parameters_by_slot:
            validator.validate(parameters)
            key = next(iter(parameters))
            for value in (True, -1, '1', None):
                self.assertTrue(list(validator.iter_errors({key: value})), (key, value))
        self.assertTrue(list(validator.iter_errors({'attackDamageReductionPercent': 101})))


if __name__ == '__main__':
    unittest.main()
