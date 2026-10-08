"""Synthetic desktop binary packaging contracts; never execute a PE or game."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from package_new_horizons_windows import stage_windows_binaries, runtime_art_identity


class WindowsClientBrandingTest(unittest.TestCase):
    def test_selected_art_metadata_makes_no_false_ownership_claim(self):
        current = runtime_art_identity()
        self.assertNotIn('proprietary_assets_included', current)
        self.assertTrue(current['original_installation_required'])
        self.assertFalse(current['unchanged_original_archives_included'])
        self.assertTrue(current['selected_original_based_and_composite_art_in_nhart'])
        self.assertFalse(runtime_art_identity(False)['selected_original_based_and_composite_art_in_nhart'])
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.install = self.root / 'install'
        self.package = self.root / 'package'
        self.install.mkdir()
        self.package.mkdir()
        (self.install / 'VCMI_lib.dll').write_bytes(b'unchanged engine DLL fixture')

    def test_current_branded_binary_is_copied_without_relabeling(self):
        payload = b'new desktop client fixture'
        (self.install / 'new-horizons.exe').write_bytes(payload)
        stage_windows_binaries(self.install, self.package)
        self.assertEqual((self.package / 'new-horizons.exe').read_bytes(), payload)
        self.assertFalse((self.package / 'VCMI_client.exe').exists())

    def test_legacy_client_is_not_an_implicit_fallback(self):
        (self.install / 'VCMI_client.exe').write_bytes(b'frozen old client fixture')
        with self.assertRaises(RuntimeError):
            stage_windows_binaries(self.install, self.package)
        self.assertEqual(list(self.package.iterdir()), [])
        stage_windows_binaries(self.install, self.package, 'VCMI_client.exe')
        self.assertTrue((self.package / 'VCMI_client.exe').is_file())
        self.assertFalse((self.package / 'new-horizons.exe').exists())

    def test_extra_executables_missing_facade_and_links_fail_before_copy(self):
        client = self.install / 'new-horizons.exe'
        client.write_bytes(b'client fixture')
        extra = self.install / 'unexpected.exe'
        extra.write_bytes(b'not a shipped component')
        with self.assertRaises(RuntimeError):
            stage_windows_binaries(self.install, self.package)
        extra.unlink()
        (self.install / 'VCMI_lib.dll').unlink()
        with self.assertRaises(RuntimeError):
            stage_windows_binaries(self.install, self.package)
        (self.install / 'VCMI_lib.dll').symlink_to(client)
        with self.assertRaises(RuntimeError):
            stage_windows_binaries(self.install, self.package)
        self.assertEqual(list(self.package.iterdir()), [])

    def test_setup_defaults_to_new_name_and_requires_exact_legacy_receipt(self):
        root = Path(__file__).resolve().parents[2]
        helper = (root / 'tools/windows/Start-New-Horizons.ps1').read_text()
        self.assertIn("$clientName = 'new-horizons.exe'", helper)
        self.assertIn("$identity.source_commit -notmatch '^[0-9a-f]{40}$'", helper)
        self.assertIn('$identity.legacy_client_source_commit -ne $identity.source_commit', helper)
        self.assertIn('foreach ($required in @($clientName,', helper)
        smoke = (root / 'tools/windows/tests/Setup-Smoke.ps1').read_text()
        self.assertIn("Join-Path $package 'new-horizons.exe'", smoke)


if __name__ == '__main__':
    unittest.main()
