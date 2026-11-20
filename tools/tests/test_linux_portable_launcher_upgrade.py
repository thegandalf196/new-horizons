"""Old installed Linux launchers pick up the selected update's launcher."""
# SPDX-License-Identifier: GPL-2.0-or-later

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/linux'))

import PortableLauncherUpgrade as upgrade

OLD_TAG = 'v0.92.5'
DIGEST = 'd' * 64


def old_source(name):
    return subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{OLD_TAG}:tools/linux/{name}'])


class LinuxPortableLauncherUpgradeTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.original = Path(temporary.name).resolve() / 'New-Horizons-Linux-x64'
        store = self.original / upgrade.STORE_NAME
        self.package = store / 'releases' / DIGEST / 'New-Horizons-Linux-x64'
        self.package.mkdir(parents=True)
        (store / 'current.json').write_text(json.dumps({'format': 1, 'version': '0.92.7', 'sha256': DIGEST}))
        for name in upgrade.FILES:
            (self.original / name).write_bytes(old_source(name))
            (self.package / name).write_bytes((ROOT / 'tools/linux' / name).read_bytes())

    def run_upgrade(self):
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            result = upgrade.upgrade_original(self.package)
        return result, errors.getvalue()

    def assert_original_is_old(self):
        for name in upgrade.FILES:
            self.assertEqual((self.original / name).read_bytes(), old_source(name), name)

    def test_old_launcher_is_upgraded_and_backed_up(self):
        self.assertEqual(self.run_upgrade(), ('upgraded', ''))
        backup = self.original / upgrade.STORE_NAME / upgrade.BACKUP_NAME
        for name in upgrade.FILES:
            self.assertEqual((self.original / name).read_bytes(), (self.package / name).read_bytes(), name)
            self.assertEqual((backup / name).read_bytes(), old_source(name), name)
        self.assertEqual(list(self.original.glob('.*.nh-upgrade')), [])
        self.assertTrue(upgrade.self_test(self.original))

    def test_upgraded_launcher_is_left_alone_afterwards(self):
        self.run_upgrade()
        before = {name: (self.original / name).stat().st_mtime_ns for name in upgrade.FILES}
        self.assertEqual(self.run_upgrade(), ('current', ''))
        self.assertEqual(before, {name: (self.original / name).stat().st_mtime_ns for name in upgrade.FILES})

    def test_failed_self_test_restores_the_previous_launcher(self):
        entry = self.package / 'PortableReleaseLauncher.py'
        entry.write_bytes(entry.read_bytes().replace(b'print(SELF_TEST)', b'print("broken")'))
        result, errors = self.run_upgrade()
        self.assertEqual(result, 'restored')
        self.assertIn('previous launcher was kept', errors)
        self.assert_original_is_old()

    def test_update_without_the_marker_changes_nothing(self):
        (self.package / 'PortableReleaseLauncher.py').write_bytes(old_source('PortableReleaseLauncher.py'))
        self.assertEqual(self.run_upgrade(), (None, ''))
        self.assert_original_is_old()

    def test_unselected_release_changes_nothing(self):
        (self.original / upgrade.STORE_NAME / 'current.json').write_text(
            json.dumps({'format': 1, 'version': '0.92.6', 'sha256': 'e' * 64}))
        self.assertEqual(self.run_upgrade(), (None, ''))
        self.assert_original_is_old()

    def test_package_outside_an_update_store_changes_nothing(self):
        self.assertIsNone(upgrade.upgrade_original(ROOT / 'tools/linux'))

    def test_linked_launcher_file_is_refused_and_the_game_still_starts(self):
        target = self.original / 'PortableLauncherWindow.py'
        real = self.original.parent / 'elsewhere.py'
        real.write_bytes(target.read_bytes())
        target.unlink()
        target.symlink_to(real)
        result, errors = self.run_upgrade()
        self.assertIsNone(result)
        self.assertIn('game starts normally', errors)
        self.assertTrue(target.is_symlink())
        self.assertEqual((self.original / 'PortableReleaseLauncher.py').read_bytes(),
                         old_source('PortableReleaseLauncher.py'))

    def test_setup_script_offers_the_upgrade_before_playing(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        import linux_assets_setup as setup
        calls = []
        module = type(sys)('PortableLauncherUpgrade')
        module.upgrade_original = calls.append
        saved = sys.modules.get('PortableLauncherUpgrade')
        sys.modules['PortableLauncherUpgrade'] = module
        try:
            setup.upgrade_launcher(self.package)
        finally:
            sys.modules['PortableLauncherUpgrade'] = saved
        self.assertEqual(calls, [self.package])


if __name__ == '__main__':
    unittest.main()
