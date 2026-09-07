#!/usr/bin/env python3
"""Execute the real shared MSVC resource staging helper against bounded fixtures."""
from pathlib import Path
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
