#!/usr/bin/env python3
"""Execute the real shared MSVC resource staging helper against bounded fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from package_new_horizons_windows import CURATED_RESOURCE_TREES, stage_engine_resources
from package_new_horizons_windows import RUNTIME_ART_PACK, verify_runtime_art, nhart
import hashlib


class CuratedResourceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.install, self.package = self.root / 'install', self.root / 'package'
        self.package.mkdir()
        for name in CURATED_RESOURCE_TREES:
            directory = self.install / name
            directory.mkdir(parents=True)
            (directory / 'fixture.json').write_text('{}')
        self.manifest = {'format': 1, 'requiredFamilies': ['fixture'],
                         'requiredFamilyCounts': {'fixture': 2}, 'entries': []}
        for name, resource, payload in (
            ('Mods/new-horizons/Images/unit.png', 'SPRITES/unit.png', b'synthetic image'),
            ('config/art.json', 'CONFIG/art.json', b'{"synthetic":true}'),
        ):
            source = self.install / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(payload)
            self.manifest['entries'].append({'source': name, 'resource': resource,
                'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
                'family': 'fixture', 'origin': 'synthetic test', 'selection': 'explicit fixture',
                'approval': 'test only'})
        nhart.pack(self.manifest, self.install, self.install / RUNTIME_ART_PACK)

    def test_includes_managed_module_excludes_optional_and_user_data(self):
        for name in ('Mods/roe-demo', 'Mods/optional', 'Data', 'Maps', 'Saves'):
            directory = self.install / name
            directory.mkdir(parents=True)
            (directory / 'must-not-ship').write_text('synthetic exclusion control')
        stage_engine_resources(self.install, self.package, self.manifest)
        self.assertTrue((self.package / 'Mods/new-horizons/fixture.json').is_file())
        self.assertEqual({p.relative_to(self.package).as_posix() for p in self.package.rglob('*') if p.is_file()},
                         {name + '/fixture.json' for name in CURATED_RESOURCE_TREES} | {RUNTIME_ART_PACK})
        self.assertEqual((self.package / RUNTIME_ART_PACK).read_bytes(),
                         (self.install / RUNTIME_ART_PACK).read_bytes())
        for entry in self.manifest['entries']:
            self.assertFalse((self.package / entry['source']).exists())
            self.assertTrue((self.install / entry['source']).is_file())
        verify_runtime_art(self.package, self.manifest)

    def test_orders_binding_and_label_survive_resource_staging(self):
        repository = Path(__file__).resolve().parents[2]
        resources = (
            'config/keyBindingsConfig.json',
            'Mods/vcmi/Content/config/translations/english.json',
        )
        for name in resources:
            target = self.install / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((repository / name).read_bytes())
        stage_engine_resources(self.install, self.package, self.manifest)
        for name in resources:
            self.assertEqual((self.package / name).read_bytes(),
                             (repository / name).read_bytes(), name)
        self.assertIn(b'"battleOpenOrders"',
                      (self.package / resources[0]).read_bytes())
        self.assertIn(b'"vcmi.keyBindings.keyBinding.battleOpenOrders"',
                      (self.package / resources[1]).read_bytes())

    def test_committed_orders_activation_and_redraw_regressions(self):
        repository = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [sys.executable, '-I', '-S', '-B',
             str(repository / 'client/tests/check-hero-action-spell-routing.py')],
            cwd=repository, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('9 in-memory mutants rejected', result.stdout)
        self.assertIn('4 missing/late-refresh mutants rejected', result.stdout)

    def test_missing_managed_module_is_fatal(self):
        (self.install / 'Mods/new-horizons/fixture.json').unlink()
        import shutil
        shutil.rmtree(self.install / 'Mods/new-horizons')
        with self.assertRaisesRegex(RuntimeError, 'Mods/new-horizons'):
            stage_engine_resources(self.install, self.package, self.manifest)

    def test_resource_symlink_is_fatal(self):
        (self.install / 'config/linked').symlink_to(self.root / 'outside')
        with self.assertRaisesRegex(RuntimeError, 'Linked engine resource'):
            stage_engine_resources(self.install, self.package, self.manifest)

    def test_missing_pack_is_not_a_loose_fallback(self):
        (self.install / RUNTIME_ART_PACK).unlink()
        with self.assertRaisesRegex(RuntimeError, 'pack missing'):
            stage_engine_resources(self.install, self.package, self.manifest)
        self.assertEqual(list(self.package.iterdir()), [])

    def test_damaged_pack_rejected_before_staging(self):
        pack = self.install / RUNTIME_ART_PACK
        data = bytearray(pack.read_bytes())
        data[nhart.HEADER.size] ^= 1
        pack.write_bytes(data)
        with self.assertRaises(nhart.NHArtError):
            stage_engine_resources(self.install, self.package, self.manifest)
        self.assertEqual(list(self.package.iterdir()), [])

    def test_expected_manifest_mismatch_rejected(self):
        import copy
        changed = copy.deepcopy(self.manifest)
        changed['entries'][0]['approval'] = 'different selection'
        with self.assertRaisesRegex(RuntimeError, 'expected source manifest'):
            stage_engine_resources(self.install, self.package, changed)

    def test_undeclared_loose_art_rejected(self):
        for tree in ('Images', 'Content/sprites', 'Content/data'):
            with self.subTest(tree=tree):
                extra = self.install / 'Mods/new-horizons' / tree / 'unlisted.png'
                extra.parent.mkdir(parents=True, exist_ok=True)
                extra.write_bytes(b'unselected')
                with self.assertRaisesRegex(RuntimeError, 'Undeclared loose'):
                    stage_engine_resources(self.install, self.package, self.manifest)
                extra.unlink()

    def test_changed_declared_loose_source_rejected(self):
        (self.install / self.manifest['entries'][0]['source']).write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'differs from selected art'):
            stage_engine_resources(self.install, self.package, self.manifest)


if __name__ == '__main__':
    unittest.main()
