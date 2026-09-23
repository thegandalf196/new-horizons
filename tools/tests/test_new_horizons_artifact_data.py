#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Artifact pool data contract; native tests cover random selection and saves."""
import unittest

from jsonschema import Draft4Validator, ValidationError
from test_new_horizons_content import load


class ArtifactPoolDataTest(unittest.TestCase):
    def test_only_retired_elemental_tomes_are_excluded(self):
        rules = load('config/newHorizonsArtifacts.json')
        self.assertEqual(set(rules['randomPoolExclusions']), {
            'core:tomeOfAirMagic', 'core:tomeOfFireMagic',
            'core:tomeOfWaterMagic', 'core:tomeOfEarthMagic',
        })
        self.assertEqual(len(rules['randomPoolExclusions']), 4)

    def test_base_mode_empty_and_module_matches_canonical(self):
        self.assertEqual(load('config/gameConfig.json')['settings']['artifacts'],
                         {'randomPoolExclusions': []})
        self.assertEqual(load('Mods/new-horizons/mod.json')['settings']['artifacts'],
                         load('config/newHorizonsArtifacts.json'))

    def test_schema_accepts_empty_override_and_rejects_malformed_ids(self):
        schema = load('config/schemas/gameSettings.json')['properties']['artifacts']
        validator = Draft4Validator(schema)
        validator.validate({'randomPoolExclusions': []})
        validator.validate(load('config/newHorizonsArtifacts.json'))
        for value in (None, [''], [12], ['tomeOfAirMagic'], ['core:tomeOfAirMagic'] * 2):
            with self.subTest(value=value), self.assertRaises(ValidationError):
                validator.validate({'randomPoolExclusions': value})


if __name__ == '__main__':
    unittest.main()
