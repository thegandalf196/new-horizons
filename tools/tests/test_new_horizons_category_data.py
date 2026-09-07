#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline partial-roster/schema/composition checks, not native or gameplay proof."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from jsonschema import Draft4Validator, ValidationError
from test_new_horizons_content import load

ROOT = Path(__file__).resolve().parents[2]


class CreatureCategoryDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = load('config/newHorizonsCreatureCategories.json')
        self.validator = Draft4Validator(load('config/schemas/newHorizonsCreatureCategories.json'))

    def test_schema_presence_and_absence(self):
        for rules in (None, {}, self.rules):
            self.validator.validate(rules)
        self.assertEqual(self.rules['sourceRulesetId'], 'new-horizons:creatureCategories')
        properties = load('config/schemas/gameSettings.json')['properties']['creatures']['properties']
        self.assertEqual(properties['newHorizonsCategories']['$ref'], 'newHorizonsCreatureCategories.json')
        self.assertEqual(load('config/gameConfig.json')['settings']['creatures']['newHorizonsCategories'], {})

    def test_partial_roster_is_explicit_not_level_inference(self):
        expected = {
            'core:pixie': 'core', 'core:sprite': 'core',
            'core:airElemental': 'elite', 'core:stormElemental': 'elite',
            'core:waterElemental': 'elite', 'core:iceElemental': 'elite',
            'core:fireElemental': 'elite', 'core:energyElemental': 'elite',
            'core:earthElemental': 'elite', 'core:magmaElemental': 'elite',
            'core:psychicElemental': 'elite', 'core:magicElemental': 'elite',
            'core:firebird': 'champion', 'core:phoenix': 'champion'}
        self.assertEqual(self.rules['creatures'], expected)
        definitions = load('config/creatures/conflux.json')
        for key in expected:
            self.assertIn(key.removeprefix('core:'), definitions)
        self.assertNotIn('core:pikeman', self.rules['creatures'])
        self.assertEqual(set(self.rules['categories']), {'core', 'elite', 'champion'})

    def test_texts_are_complete_and_separate(self):
        texts = load('config/newHorizonsCreatureCategoryTexts.json')
        ids = {value for definition in self.rules['categories'].values() for value in definition.values()}
        self.assertEqual(set(texts), ids)
        self.assertTrue(all(isinstance(text, str) and text for text in texts.values()))
        self.assertFalse(set(texts) & set(load('config/newHorizonsMasteryTexts.json')))

    def test_malformed_shapes_fail_without_restricting_entity_keys(self):
        changes = [
            lambda r: r.update(schemaVersion=2),
            lambda r: r.update(unreviewed=True),
            lambda r: r.update(creatures={}),
            lambda r: r['creatures'].update({'core:pixie': 1}),
            lambda r: r['creatures'].update({'core:pixie': 'boss'}),
            lambda r: r['categories'].pop('champion'),
            lambda r: r['categories']['core'].update(nameTextId=''),
            lambda r: r['categories']['elite'].update(numericalTier=4)]
        for change in changes:
            rules = copy.deepcopy(self.rules)
            change(rules)
            with self.assertRaises(ValidationError):
                self.validator.validate(rules)
        rules = copy.deepcopy(self.rules)
        rules['creatures'] = {'core:notAnActualCreature': 'core'}
        self.validator.validate(rules)  # Runtime resolves canonical entity identity.

    def test_private_composition_and_write_guards(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-categories.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            preview = json.loads(output.read_text())
            baseline = json.loads(before)
            self.assertEqual(preview['version'], '0.6.0')
            self.assertEqual(preview['settings']['creatures'], {'newHorizonsCategories': self.rules})
            self.assertIn('provisionally Firebird', preview['description'])
            for key in baseline['settings']:
                self.assertEqual(preview['settings'][key], baseline['settings'][key])
            self.assertEqual(preview['bonuses'], baseline['bonuses'])
            self.assertEqual(preview['filesystem'], baseline['filesystem'])
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            link = Path(temporary) / 'linked.json'
            link.symlink_to(output)
            self.assertNotEqual(subprocess.run([sys.executable, str(script), '--output', str(link), '--check'], capture_output=True).returncode, 0)
            output.write_text('{}')
            self.assertNotEqual(subprocess.run(command + ['--check'], capture_output=True).returncode, 0)
        with tempfile.TemporaryDirectory() as outside:
            for flags in ([], ['--check']):
                with self.subTest(outside_mode=flags):
                    missing = Path(outside) / ('outside-check.json' if flags else 'outside-new.json')
                    result = subprocess.run([sys.executable, str(script), '--output', str(missing), *flags], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('preview output must remain under build/', result.stderr)
                    self.assertFalse(missing.exists())
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--output', str(live)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertEqual(json.loads(before)['version'], '0.5.1')
        self.assertNotIn('creatures', json.loads(before)['settings'])


if __name__ == '__main__':
    unittest.main()
