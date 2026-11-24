#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Captured Sorcery schema checks; no gameplay claim."""
import copy
import unittest
from pathlib import Path

from test_new_horizons_content import load, magic_validators

ROOT = Path(__file__).resolve().parents[2]


class SorceryRevisionSchemaTest(unittest.TestCase):
    def test_revised_default_and_historical_absence_validate(self):
        validator = magic_validators()[2]
        rules = load('config/newHorizonsMagic.json')
        validator.validate(rules)
        self.assertEqual(rules['spells']['core:slow']['level'], 3)
        self.assertEqual(rules['spells']['new-horizons:massSlow']['costs'], [33] * 4)
        self.assertFalse(rules['spells']['core:haste']['ordinaryAcquisition'])
        self.assertIs(rules['spells']['core:haste']['heroAccess'], False)
        del rules['spells']['core:haste']['heroAccess']  # Earlier captured Haste exception remains valid.
        del rules['spells']['new-horizons:focusMagic']['focusMagic']
        del rules['spells']['core:slow']['slow']
        del rules['spells']['new-horizons:summonLivingArmor']
        validator.validate(rules)

    def test_rejects_null_unknown_fields_and_bad_profile_domains(self):
        validator = magic_validators()[2]
        rules = load('config/newHorizonsMagic.json')
        for identity, clause in (('core:slow', 'slow'),
                                 ('new-horizons:focusMagic', 'focusMagic'),
                                 ('new-horizons:summonLivingArmor', 'livingArmor')):
            profile = rules['spells'][identity][clause]
            for invalid in (None, False, {}, {**profile, 'version': 2},
                            {**profile, 'extra': 1}):
                changed = copy.deepcopy(rules)
                changed['spells'][identity][clause] = invalid
                with self.subTest(identity=identity, invalid=invalid):
                    self.assertFalse(validator.is_valid(changed))



if __name__ == '__main__':
    unittest.main()
