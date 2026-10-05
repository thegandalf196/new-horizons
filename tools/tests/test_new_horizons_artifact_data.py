#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Artifact pool data contract; native tests cover random selection and saves."""
import unittest

from jsonschema import Draft4Validator, ValidationError
from test_new_horizons_content import load


class ArtifactPoolDataTest(unittest.TestCase):
    def test_adventure_movement_descriptions_are_registered_without_replacing_legacy_bonuses(self):
        path = 'config/artifacts/adventureMovementCasts.json'
        overlay = load('Mods/new-horizons/Content/' + path)
        self.assertIn(path, load('Mods/new-horizons/mod.json')['artifacts'])
        self.assertEqual(set(overlay), {'core:bootsOfLevitation', 'core:angelWings'})
        for artifact, spell, cost in (
                ('core:bootsOfLevitation', 'Water Walk', 20),
                ('core:angelWings', 'Fly', 40)):
            with self.subTest(artifact=artifact):
                self.assertEqual(set(overlay[artifact]), {'text'})
                text = overlay[artifact]['text']['description']
                self.assertIn(spell, text)
                self.assertIn(f'{cost} Spell Points', text)
                self.assertIn('one Adventure Spell for the day', text)
                self.assertIn('without learning', text)
        core = load('config/artifacts.json')
        self.assertEqual(core['bootsOfLevitation']['bonuses']['waterWalking']['type'], 'WATER_WALKING')
        self.assertEqual(core['angelWings']['bonuses']['fly']['type'], 'FLYING_MOVEMENT')

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

    def test_schema_accepts_string_identifiers_and_rejects_wrong_shapes(self):
        schema = load('config/schemas/gameSettings.json')['properties']['artifacts']
        validator = Draft4Validator(schema)
        validator.validate({'randomPoolExclusions': []})
        validator.validate(load('config/newHorizonsArtifacts.json'))
        # The native schema validator deliberately does not implement JSON
        # Schema regex patterns. Identifier scope/existence is checked by the
        # runtime loader; this structural test only owns type, length, and
        # uniqueness.
        validator.validate({'randomPoolExclusions': ['tomeOfAirMagic']})
        for value in (None, [''], [12], ['core:tomeOfAirMagic'] * 2):
            with self.subTest(value=value), self.assertRaises(ValidationError):
                validator.validate({'randomPoolExclusions': value})


if __name__ == '__main__':
    unittest.main()
