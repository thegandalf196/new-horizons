#!/usr/bin/env python3
"""Execute the real shared MSVC resource staging helper against bounded fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from package_new_horizons_windows import CURATED_RESOURCE_TREES, stage_engine_resources


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

    def test_includes_managed_module_excludes_optional_and_user_data(self):
        for name in ('Mods/roe-demo', 'Mods/optional', 'Data', 'Maps', 'Saves'):
            directory = self.install / name
            directory.mkdir(parents=True)
            (directory / 'must-not-ship').write_text('synthetic exclusion control')
        stage_engine_resources(self.install, self.package)
        self.assertTrue((self.package / 'Mods/new-horizons/fixture.json').is_file())
        self.assertEqual({p.relative_to(self.package).as_posix() for p in self.package.rglob('*') if p.is_file()},
                         {name + '/fixture.json' for name in CURATED_RESOURCE_TREES})

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
        stage_engine_resources(self.install, self.package)
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
        (self.install / 'Mods/new-horizons').rmdir()
        with self.assertRaisesRegex(RuntimeError, 'Mods/new-horizons'):
            stage_engine_resources(self.install, self.package)

    def test_resource_symlink_is_fatal(self):
        (self.install / 'config/linked').symlink_to(self.root / 'outside')
        with self.assertRaisesRegex(RuntimeError, 'Linked engine resource'):
            stage_engine_resources(self.install, self.package)


if __name__ == '__main__':
    unittest.main()
