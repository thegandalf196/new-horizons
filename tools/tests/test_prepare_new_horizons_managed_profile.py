#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline fixture-composition/security tests. No compiler, executable or GUI."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SOURCE = Path(__file__).with_name('prepare_new_horizons_managed_profile.py')
spec = importlib.util.spec_from_file_location('managed_profile', SOURCE)
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class ManagedProfileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.inputs, cls.spell = profile.load_inputs(profile.ROOT)

    def test_exact_pinned_keysets(self):
        self.assertEqual(len(self.manifest['player_resources']), 945)
        self.assertEqual(len(self.manifest['test_resources']), 18)
        self.assertNotIn('config/newHorizonsSpells.json', self.inputs)
        self.assertEqual(profile.digest(self.spell), profile.SPELL_SHA256)

    def test_only_module_and_wrapper_change(self):
        result = profile.compose(self.inputs, self.spell)
        changed = {name for name in result if result[name] != self.inputs[name]}
        self.assertEqual(changed, {'Mods/new-horizons/mod.json', 'config/schemas/gameSettings.json'})
        module = json.loads(result['Mods/new-horizons/mod.json'])
        rules = module['settings']['magic']['newHorizons']
        self.assertEqual((module['version'], rules['rulesetVersion'], len(rules['spells'])), ('0.7.0', 2, 70))
        self.assertEqual(rules['spells']['new-horizons:magicMissile']['directDamage'],
                         {'base': 20, 'powerCoefficient': 20})
        original = json.loads(self.inputs['Mods/new-horizons/mod.json'])
        self.assertEqual(original['version'], '0.5.1')
        self.assertNotIn('spells', original)

    def test_wrong_spell_cost_rejected(self):
        spell = json.loads(self.spell)
        spell['magicMissile']['levels']['basic']['cost'] = 6
        with self.assertRaisesRegex(ValueError, 'costs'):
            profile.compose(self.inputs, profile.encode(spell))

    def test_already_activated_module_rejected(self):
        inputs = dict(self.inputs)
        module = json.loads(inputs['Mods/new-horizons/mod.json'])
        module['spells'] = {}
        inputs['Mods/new-horizons/mod.json'] = profile.encode(module)
        with self.assertRaisesRegex(ValueError, 'default module'):
            profile.compose(inputs, self.spell)

    def test_duplicate_keys_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            profile.decode('{"a": 1, "a": 2}')

    def test_source_hash_size_and_symlink_guards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'good').write_bytes(b'good')
            self.assertEqual(profile.checked_source(root, 'good', profile.digest(b'good'), 4), b'good')
            for checksum, size in [(profile.digest(b'evil'), 4), (profile.digest(b'good'), 3)]:
                with self.assertRaises(ValueError):
                    profile.checked_source(root, 'good', checksum, size)
            (root / 'alias').symlink_to(root / 'good')
            with self.assertRaisesRegex(ValueError, 'symlink'):
                profile.checked_source(root, 'alias', profile.digest(b'good'), 4)
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                profile.checked_source(root, '../good', profile.digest(b'good'), 4)

    def test_output_boundary_overlap_and_symlink_guards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = root / 'purchaser'; data.mkdir()
            build = root / 'build'; build.mkdir()
            for output in [Path('relative'), root / 'outside', build / '..' / 'outside', data / 'profile']:
                with self.assertRaises(ValueError):
                    profile.checked_output(root, output, data)
            (build / 'alias').symlink_to(data, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                profile.checked_output(root, build / 'alias' / 'profile', data)
            with self.assertRaisesRegex(ValueError, 'overlap'):
                profile.checked_output(root, build / 'profile', root)

    def test_actual_data_composition_and_no_overwrite(self):
        build = profile.ROOT / 'build'; build.mkdir(exist_ok=True)
        before = (profile.ROOT / 'Mods/new-horizons/mod.json').read_bytes()
        with tempfile.TemporaryDirectory(prefix='managed-profile-unit-', dir=build) as temporary:
            folder = Path(temporary)
            data = folder / 'purchaser'; data.mkdir()
            output = folder / 'profile'
            identity = profile.prepare(profile.ROOT, output, data)
            self.assertEqual(identity['player_resource_count'], 945)
            self.assertEqual(identity['composer_sha256'], profile.digest(SOURCE.read_bytes()))
            self.assertEqual(identity['input_resource_commit'], self.manifest['source_commit'])
            self.assertNotIn('source_commit', identity)
            self.assertEqual(len(identity['files']), 966)
            for name, record in identity['files'].items():
                path = output / name
                if 'symlink' in record:
                    self.assertTrue(path.is_symlink())
                    self.assertEqual(str(path.readlink()), record['symlink'])
                else:
                    self.assertEqual(profile.digest(path.read_bytes()), record['sha256'])
                    self.assertEqual(path.stat().st_size, record['size'])
                    self.assertNotIn(path.suffix.lower(), {'.exe', '.dll', '.so', '.o', '.a'})
            preset = json.loads((output / 'config/vcmi/testModSettings.json').read_bytes())
            self.assertEqual(preset['presets'][preset['activePreset']]['mods'],
                             ['core', 'vcmi', 'vcmi-test', 'new-horizons'])
            self.assertFalse((output / 'config/vcmi/modSettings.json').exists())
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                profile.prepare(profile.ROOT, output, data)
        self.assertEqual((profile.ROOT / 'Mods/new-horizons/mod.json').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
