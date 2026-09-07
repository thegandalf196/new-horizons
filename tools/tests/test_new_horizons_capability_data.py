"""Unactivated capability data checks, not native or GUI acceptance."""
import json
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


class CapabilityDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = json.loads((ROOT / 'config/newHorizonsCapabilities.json').read_text())

    def test_explicit_provisional_class_profiles(self):
        classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        self.assertEqual(set(self.rules['classProfiles']), {'core:' + name for name in classes})
        for name, definition in classes.items():
            expected = {'base': 750, 'perLevel': 75} if definition['affinity'] == 'might' else {'base': 500, 'perLevel': 50}
            self.assertEqual(self.rules['classProfiles']['core:' + name], expected)

    def test_rank_tables_and_soft_floor(self):
        self.assertEqual(self.rules['schemaVersion'], 1)
        self.assertEqual(self.rules['rulesetVersion'], 1)
        self.assertEqual(self.rules['leadership']['skillBonusPercent'], [0, 25, 50, 100])
        self.assertEqual(self.rules['leadership']['minimumMovementPercent'], 50)
        self.assertEqual(self.rules['siege']['ballistaDamageMultiplier'], [1, 2, 3, 4])

    def test_private_capability_generation_preserves_live_module(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--capability-preview-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = json.loads(output.read_text())
            self.assertEqual(metadata['version'], '0.4.0')
            self.assertEqual(metadata['settings']['heroes']['newHorizonsCapabilities'], self.rules)
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--capability-preview-output', str(live)], capture_output=True).returncode, 0)

    def test_capability_only_control_is_separate_and_non_overwriting(self):
        script = ROOT / 'tools/update-new-horizons-module.py'
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'control.json'
            command = [sys.executable, str(script), '--capability-only-control-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = json.loads(output.read_text())
            self.assertEqual(metadata['settings']['heroes']['newHorizons'], {})
            self.assertEqual(metadata['settings']['heroes']['newHorizonsCapabilities'], self.rules)
            self.assertIn('diagnostic', metadata['name'])
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(command + ['--capability-preview-output', str(output)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--capability-only-control-output', str(live)], capture_output=True).returncode, 0)

    def test_schema_registered_but_default_module_unactivated(self):
        schema = json.loads((ROOT / 'config/schemas/gameSettings.json').read_text())
        self.assertEqual(schema['properties']['heroes']['properties']['newHorizonsCapabilities']['$ref'], 'newHorizonsCapabilities.json')
        module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        self.assertNotIn('newHorizonsCapabilities', module['settings']['heroes'])


if __name__ == '__main__':
    unittest.main()
