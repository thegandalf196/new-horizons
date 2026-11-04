#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Current concise help, optional precise details and shipped-registry parity."""
import copy
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]


class PlainHelpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())
        cls.schema = json.loads((ROOT / 'config/schemas/newHorizonsPerks.json').read_text())

    def test_all_current_perk_summaries_are_short_with_matching_effect_text(self):
        for skill in self.rules['skills'].values():
            for perk in skill['perks']:
                with self.subTest(perk=perk['id']):
                    self.assertLessEqual(len(perk['description'].split()), 25)
                    self.assertNotIn('\n', perk['description'])
                    self.assertEqual(perk['effect']['description'], perk['description'])

    def test_optional_details_reject_empty_or_non_string_values(self):
        validator = Draft4Validator(self.schema)
        for level in ('skill', 'rank', 'perk'):
            for invalid in ('', None, 1, True, [], {}):
                rules = copy.deepcopy(self.rules)
                skill = rules['skills']['new-horizons:offense']
                target = (skill if level == 'skill' else skill['ranks']['basic']
                          if level == 'rank' else skill['perks'][0])
                target['details'] = invalid
                with self.subTest(level=level, invalid=invalid):
                    self.assertTrue(list(validator.iter_errors(rules)))

    def test_precise_rounding_and_lifecycle_rules_remain_in_details(self):
        perks = {p['id'].split(':')[1]: p for s in self.rules['skills'].values()
                 for p in s['perks']}
        self.assertIn('floor(actual reflected HP damage / 4)',
                      perks['bulwarkOfTheMire.toxicSpines']['details'])
        self.assertIn('Cure removes it; Dispel does not',
                      perks['bulwarkOfTheMire.toxicSpines']['details'])
        self.assertIn('cannot trigger itself', perks['offense.cleave']['details'])
        self.assertIn('restarts its duration',
                      perks['bulwarkOfTheMire.toxicSpines']['details'])
        self.assertIn('Count each spell independently',
                      perks['wisdom.spellFamiliarity']['details'])

    def test_generated_mod_and_skill_summaries_match_current_canon_data(self):
        module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        skills = json.loads((ROOT / 'config/newHorizonsSkills.json').read_text())
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks'], self.rules)
        self.assertEqual(module['skills'], skills)
        for sid, skill in self.rules['skills'].items():
            for rank in ('basic', 'advanced', 'expert'):
                self.assertEqual(skill['ranks'][rank]['description'],
                                 skills[sid.split(':')[1]][rank]['description'])


if __name__ == '__main__':
    unittest.main()
