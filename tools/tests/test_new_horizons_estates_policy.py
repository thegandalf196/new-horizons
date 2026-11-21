#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Captured Estates catalogue admission; native execution is a separate check."""
import copy
import json
from pathlib import Path
import unittest
from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'new-horizons:estates.'
if __package__:
    from .nh_estates_captures import legacy_estates_perks
else:
    from nh_estates_captures import legacy_estates_perks


def load(relative):
    return json.loads((ROOT / relative).read_text(encoding='utf-8'))


def revised(rules):
    result = copy.deepcopy(rules)
    result['estatesPolicyVersion'] = 1
    replacements = {'landSurveyor': 'supervisor', 'prosperity': 'mercenaryCompany',
                    'mercenaryCompany': 'hoarder', 'banking': 'decentralization'}
    for row in result['skills']['new-horizons:estates']['perks']:
        identity = row['id'].removeprefix(PREFIX)
        row['id'] = PREFIX + replacements.get(identity, identity)
    return result


class EstatesPolicyContentTest(unittest.TestCase):
    def setUp(self):
        self.rules = legacy_estates_perks(load('config/newHorizonsPerks.json'))
        self.validator = Draft4Validator(load('config/schemas/newHorizonsPerks.json'))

    def test_historical_catalogue_is_accepted_without_reinterpretation(self):
        self.assertNotIn('estatesPolicyVersion', self.rules)
        self.validator.validate(self.rules)
        rows = {row['id']: row for row in self.rules['skills']['new-horizons:estates']['perks']}
        self.assertEqual(rows[PREFIX + 'mercenaryCompany']['requires'], 'advanced')
        for identity in ('landSurveyor', 'prosperity', 'banking'):
            self.assertIn(PREFIX + identity, rows)

    def test_revised_catalogue_requires_its_own_marker_and_current_tiers(self):
        current = revised(self.rules)
        self.validator.validate(current)
        rows = current['skills']['new-horizons:estates']['perks']
        for row in rows:
            if row['id'] == PREFIX + 'mercenaryCompany':
                row['requires'] = 'advanced'
        self.assertFalse(self.validator.is_valid(current))

    def test_core_stewardship_marker_uses_the_revised_catalogue(self):
        current = revised(self.rules)
        current['estatesPolicyVersion'] = 2
        self.validator.validate(current)
        candidate = copy.deepcopy(self.rules)
        candidate['estatesPolicyVersion'] = 2
        self.assertFalse(self.validator.is_valid(candidate))

    def test_marker_cannot_relabel_the_old_catalogue(self):
        candidate = copy.deepcopy(self.rules)
        candidate['estatesPolicyVersion'] = 1
        self.assertFalse(self.validator.is_valid(candidate))

    def test_unknown_or_wrong_typed_markers_are_rejected(self):
        for version in (None, False, True, 0, 3, 1.5, '1', {}, []):
            with self.subTest(version=version):
                candidate = revised(self.rules)
                candidate['estatesPolicyVersion'] = version
                self.assertFalse(self.validator.is_valid(candidate))

    def test_revised_catalogue_rejects_retired_and_unknown_identities(self):
        for identity in ('landSurveyor', 'prosperity', 'banking', 'unsupported'):
            candidate = revised(self.rules)
            candidate['skills']['new-horizons:estates']['perks'][0]['id'] = PREFIX + identity
            self.assertFalse(self.validator.is_valid(candidate))


if __name__ == '__main__':
    unittest.main()
