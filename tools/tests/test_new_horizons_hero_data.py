#!/usr/bin/env python3
"""Hero-data shape checks; native resolution and GUI acceptance are separate gates."""
import json
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[2]


class HeroDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = json.loads((ROOT / 'config/newHorizonsHeroes.json').read_text())

    def test_all_core_classes_have_explicit_provisional_profiles(self):
        classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        self.assertEqual(set(self.rules['classProfiles']), {'core:' + name for name in classes})
        for profile in self.rules['classProfiles'].values():
            self.assertEqual(sorted(profile['starting']), [5, 10, 15, 20])
            self.assertEqual(len(profile['growth']), 4)
            self.assertTrue(all(isinstance(value, int) and value >= 1 for value in profile['growth']))
            self.assertEqual(sum(profile['growth']), 10)

    def test_preview_generation_is_separate_and_never_overwrites(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--hero-preview-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            generated = json.loads(output.read_text())
            self.assertEqual(generated['settings']['heroes']['newHorizons'], self.rules)
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--hero-preview-output', str(live)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertEqual(json.loads(before)['settings']['heroes']['newHorizons'], self.rules)
        subprocess.run([sys.executable, str(script), '--check'], check=True, capture_output=True)

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required for native module drift guard')
    def test_cmake_guard_accepts_activation_and_rejects_hero_drift(self):
        source = (ROOT / 'CMakeLists.txt').read_text()
        block = source.split('# Curated settings are authored once;', 1)[1]
        block = block[block.index('if(EXISTS'):].split('\nif(ANDROID)', 1)[0]
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            (root / 'config').mkdir()
            (root / 'Mods/new-horizons').mkdir(parents=True)
            for name in ('Combat', 'Magic', 'Schools', 'Skills', 'Heroes'):
                shutil.copyfile(ROOT / f'config/newHorizons{name}.json', root / f'config/newHorizons{name}.json')
            shutil.copyfile(ROOT / 'Mods/new-horizons/mod.json', root / 'Mods/new-horizons/mod.json')
            script = root / 'check.cmake'
            script.write_text(f'set(CMAKE_SOURCE_DIR "{root.as_posix()}")\n' + block)
            command = ['cmake', '-P', str(script)]
            subprocess.run(command, check=True, capture_output=True)
            rules = json.loads((root / 'config/newHorizonsHeroes.json').read_text())
            rules['maxPrimary'] += 1
            (root / 'config/newHorizonsHeroes.json').write_text(json.dumps(rules))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Stale curated module settings', result.stderr)

    def test_scale_and_cap_are_explicit(self):
        self.assertEqual(self.rules['schemaVersion'], 1)
        self.assertEqual(self.rules['rulesetVersion'], 1)
        self.assertTrue(1 <= self.rules['powerDivisor'] <= 1000)
        self.assertTrue(100 <= self.rules['maxPrimary'] <= 1000000)

    def test_independent_extras_use_owned_skill_ranks_only(self):
        seen = set()
        for extra in self.rules['extraGrowth']:
            self.assertTrue(extra['skill'].startswith('core:'))
            self.assertNotIn(extra['skill'], seen)
            seen.add(extra['skill'])
            self.assertIn(extra['primary'], range(4))
            self.assertEqual(len(extra['chances']), 4)
            self.assertEqual(extra['chances'][0], 0)
            self.assertTrue(all(isinstance(value, int) and 0 <= value <= 100 for value in extra['chances']))


if __name__ == '__main__':
    unittest.main()
