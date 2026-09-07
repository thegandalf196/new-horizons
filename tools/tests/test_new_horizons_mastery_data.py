#!/usr/bin/env python3
"""Future mastery data shape only; no progression/effect/AI/GUI acceptance."""
import json
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    return json.loads((ROOT / name).read_text())


class MasteryDataTest(unittest.TestCase):
    def test_first_family_has_three_explicit_distinct_options(self):
        rules = load('config/newHorizonsMasteries.json')
        self.assertEqual((rules['schemaVersion'], rules['rulesetVersion']), (1, 1))
        self.assertEqual(set(rules['skills']), {'core:artillery'})
        options = rules['skills']['core:artillery']['options']
        self.assertEqual(len(options), 3)
        self.assertEqual({o['effect']: o['magnitude'] for o in options},
                         {'volley': 1, 'precision': 1, 'repair': 50})
        self.assertEqual(len({o['id'] for o in options}), 3)
        for option in options:
            self.assertEqual(set(option), {'id', 'effect', 'magnitude', 'nameTextId', 'descriptionTextId', 'iconKey'})
            self.assertTrue(option['id'].startswith('new-horizons:artillery'))
            self.assertEqual(option['iconKey'], 'NH_mastery_' + option['id'].split(':')[1])
            self.assertRegex(option['iconKey'], r'^[A-Za-z][A-Za-z0-9_]*$')

    def test_text_identity_and_saved_magnitude_placeholders(self):
        texts = load('config/newHorizonsMasteryTexts.json')
        options = load('config/newHorizonsMasteries.json')['skills']['core:artillery']['options']
        expected = {o[k] for o in options for k in ('nameTextId', 'descriptionTextId')}
        self.assertEqual(set(texts), expected)
        for option in options:
            self.assertTrue(texts[option['nameTextId']])
            self.assertEqual('{magnitude}' in texts[option['descriptionTextId']], option['effect'] != 'precision')

    def test_private_generator_preserves_live_module_and_refuses_overwrite(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--mastery-preview-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = json.loads(output.read_text())
            self.assertEqual(metadata['version'], '0.5.0')
            self.assertEqual(metadata['settings']['heroes']['newHorizonsMasteries'], load('config/newHorizonsMasteries.json'))
            self.assertEqual(metadata['translations'], load('config/newHorizonsMasteryTexts.json'))
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(command + ['--hero-preview-output', str(output)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--mastery-preview-output', str(live)], capture_output=True).returncode, 0)

    def test_icon_schema_uses_supported_enum_not_unimplemented_pattern(self):
        schema = load('config/schemas/newHorizonsMasteries.json')
        icon = schema['definitions']['option']['properties']['iconKey']
        self.assertNotIn('pattern', icon)
        self.assertEqual(icon['type'], 'string')
        self.assertEqual(set(icon['enum']), {
            'NH_mastery_artilleryVolley', 'NH_mastery_artilleryPrecision',
            'NH_mastery_artilleryRepair'})
        self.assertNotIn('../unsafe', icon['enum'])

    def test_schema_registration_and_default_mastery_identity(self):
        settings = load('config/schemas/gameSettings.json')
        self.assertEqual(settings['properties']['heroes']['properties']['newHorizonsMasteries']['$ref'],
                         'newHorizonsMasteries.json')
        schema = load('config/schemas/newHorizonsMasteries.json')
        self.assertEqual(schema['definitions']['option']['properties']['effect']['enum'],
                         ['volley', 'precision', 'repair'])
        module = load('Mods/new-horizons/mod.json')
        self.assertEqual(module['version'], '0.5.1')
        self.assertEqual(module['settings']['heroes']['newHorizonsMasteries'], load('config/newHorizonsMasteries.json'))
        self.assertEqual(module['translations'], load('config/newHorizonsMasteryTexts.json'))


if __name__ == '__main__':
    unittest.main()
