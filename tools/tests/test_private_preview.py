import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('private_preview', Path(__file__).parents[1] / 'ci/package_private_preview.py')
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)


class PrivatePreviewTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.engine = self.root / 'engine'
        self.images = self.root / 'images'
        self.images.mkdir()
        (self.engine / 'Mods/new-horizons/Images').mkdir(parents=True)
        (self.engine / 'Mods/new-horizons/mod.json').write_text(json.dumps({'spellSchools': {s: {'schoolBorders': 'old'} for s in recipe.SCHOOLS}}))
        (self.engine / 'BUILD-IDENTITY.json').write_text('{"source":"test-source"}')
        for name in ('Play-New-Horizons.sh', 'new-horizons-launch.sh'):
            (self.engine / name).write_text('#!/bin/sh\nexit 0\n')
            (self.engine / name).chmod(0o755)
        for n in ('vcmiclient', 'libvcmi.so'):
            (self.engine / n).write_bytes(b'unchanged engine fixture')
        (self.engine / 'SHA256SUMS').write_text(''.join(h + '  ' + n + '\n' for n, h in recipe.inventory(self.engine).items()))
        for school in recipe.SCHOOLS:
            for i in range(24):
                (self.images / f'NH_{school}_{i}.png').write_bytes(b'\x89PNG\r\n\x1a\nfixture')
            for kind in ('bookmark', 'button', 'spellBorders'):
                (self.images / f'NH_{school}_{kind}.json').write_text(json.dumps({'images': [{'file': f'NH_{school}_0.png', 'frame': 0, 'group': 0}]}))
        self.manifest = self.root / 'manifest.json'
        self.manifest.write_text(json.dumps({'files': recipe.inventory(self.images)}))
        self.pin = recipe.digest(self.manifest)

    def run_package(self, name='out'):
        return recipe.package(self.engine, self.images, self.manifest, self.pin, 'test-source', 'linux', self.root / name)

    def refresh_linux_checksums(self):
        (self.engine / 'SHA256SUMS').write_text(''.join(
            h + '  ' + n + '\n' for n, h in recipe.inventory(self.engine).items()
            if n != 'SHA256SUMS'))

    def test_current_linux_name_is_preserved_in_inventory_and_archive(self):
        (self.engine / 'vcmiclient').rename(self.engine / 'new-horizons')
        self.refresh_linux_checksums()
        before = recipe.inventory(self.engine)
        result = self.run_package()
        stage = self.root / 'out/New-Horizons-Private-Preview'
        self.assertEqual(result['engine_binaries']['new-horizons'], before['new-horizons'])
        self.assertNotIn('vcmiclient', result['engine_binaries'])
        self.assertEqual(recipe.digest(stage / 'new-horizons'), before['new-horizons'])
        self.assertEqual(recipe.digest(stage / 'libvcmi.so'), before['libvcmi.so'])
        self.assertFalse((stage / 'vcmiclient').exists())
        self.assertEqual(before, recipe.inventory(self.engine))
        with recipe.tarfile.open(self.root / 'out' / result['archive']) as archive:
            self.assertIn(stage.name + '/new-horizons', archive.getnames())
            self.assertNotIn(stage.name + '/vcmiclient', archive.getnames())

    def test_ambiguous_linux_clients_rejected_before_output(self):
        (self.engine / 'new-horizons').write_bytes(b'Other client fixture')
        self.refresh_linux_checksums()
        before = recipe.inventory(self.engine)
        with self.assertRaisesRegex(ValueError, 'ambiguous Linux client identity'):
            self.run_package()
        self.assertFalse((self.root / 'out').exists())
        self.assertEqual(before, recipe.inventory(self.engine))

    def test_current_linux_client_still_requires_matching_library(self):
        (self.engine / 'vcmiclient').rename(self.engine / 'new-horizons')
        (self.engine / 'libvcmi.so').unlink()
        self.refresh_linux_checksums()
        with self.assertRaisesRegex(ValueError, 'Linux client identity'):
            self.run_package()
        self.assertFalse((self.root / 'out').exists())

    def test_current_linux_client_checksum_tampering_is_rejected(self):
        (self.engine / 'vcmiclient').rename(self.engine / 'new-horizons')
        self.refresh_linux_checksums()
        (self.engine / 'new-horizons').write_bytes(b'Tampered client fixture')
        with self.assertRaisesRegex(ValueError, 'inventory/checksum mismatch'):
            self.run_package()
        self.assertFalse((self.root / 'out').exists())

    def test_nhart_engine_remains_ineligible_for_historical_overlay(self):
        (self.engine / 'Mods/new-horizons/NewHorizons.nhart').write_bytes(b'Synthetic marker')
        with self.assertRaisesRegex(ValueError, 'cannot overlay an NHART engine'):
            self.run_package()
        self.assertFalse((self.root / 'out').exists())

    def test_missing_launcher_rejected_before_output(self):
        (self.engine / 'Play-New-Horizons.sh').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing required launcher'):
            self.run_package()
        self.assertFalse((self.root / 'out').exists())

    def test_overlapping_output_rejected_before_write(self):
        with self.assertRaisesRegex(ValueError, 'overlaps protected input'):
            recipe.package(self.engine, self.images, self.manifest, self.pin, 'test-source', 'linux', self.engine / 'nested')
        self.assertFalse((self.engine / 'nested').exists())

    def test_missing_overlay_rejected(self):
        next(self.images.glob('*.png')).unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or changed'):
            self.run_package()
        self.assertFalse((self.root / 'out').exists())

    def test_changed_overlay_rejected(self):
        next(self.images.glob('*.png')).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Missing or changed'):
            self.run_package()

    def test_manifest_pin_rejected(self):
        self.pin = '0' * 64
        with self.assertRaisesRegex(ValueError, 'manifest digest'):
            self.run_package()

    def test_windows_contract_and_determinism(self):
        (self.engine / 'SHA256SUMS').unlink()
        (self.engine / 'new-horizons.exe').write_bytes(b'Windows engine fixture')
        (self.engine / 'Play-New-Horizons.cmd').write_text('@echo off\n')
        (self.engine / 'Start-New-Horizons.ps1').write_text("$profile = 'HeroesIII-NewHorizons'\n")
        (self.engine / 'config').mkdir()
        (self.engine / 'config/dirs.json').write_text('{"path":"HeroesIII-NewHorizons"}')
        (self.engine / 'SHA256SUMS.txt').write_text(''.join(h + '  ' + n + '\n' for n, h in recipe.inventory(self.engine).items()))
        before = recipe.inventory(self.engine)
        a = recipe.package(self.engine, self.images, self.manifest, self.pin, 'test-source', 'windows', self.root / 'win-a')
        b = recipe.package(self.engine, self.images, self.manifest, self.pin, 'test-source', 'windows', self.root / 'win-b')
        self.assertEqual(a['archive_sha256'], b['archive_sha256'])
        self.assertEqual(before, recipe.inventory(self.engine))
        self.assertEqual(a['engine_binaries']['new-horizons.exe'], before['new-horizons.exe'])
        self.assertIn('new-horizons-private-', (self.root / 'win-a/New-Horizons-Private-Preview/config/dirs.json').read_text())

    def test_windows_legacy_requires_explicit_matching_source_pin_and_keeps_name(self):
        source = 'a' * 40
        (self.engine / 'BUILD-IDENTITY.json').write_text(json.dumps({'source': source}))
        (self.engine / 'SHA256SUMS').unlink()
        (self.engine / 'VCMI_client.exe').write_bytes(b'Frozen legacy Windows fixture')
        (self.engine / 'Play-New-Horizons.cmd').write_text('@echo off\n')
        (self.engine / 'Start-New-Horizons.ps1').write_text("$profile = 'HeroesIII-NewHorizons'\n")
        (self.engine / 'config').mkdir()
        (self.engine / 'config/dirs.json').write_text('{"path":"HeroesIII-NewHorizons"}')
        (self.engine / 'SHA256SUMS.txt').write_text(''.join(h + '  ' + n + '\n' for n, h in recipe.inventory(self.engine).items()))
        for pin in (None, 'b' * 40):
            with self.subTest(pin=pin), self.assertRaises(ValueError):
                recipe.package(self.engine, self.images, self.manifest, self.pin, source,
                               'windows', self.root / 'rejected', legacy_frozen_client_source=pin)
            self.assertFalse((self.root / 'rejected').exists())
        recipe.package(self.engine, self.images, self.manifest, self.pin, source,
                       'windows', self.root / 'legacy', legacy_frozen_client_source=source)
        stage = self.root / 'legacy/New-Horizons-Private-Preview'
        self.assertTrue((stage / 'VCMI_client.exe').is_file())
        self.assertFalse((stage / 'new-horizons.exe').exists())
        identity = json.loads((stage / 'BUILD-IDENTITY.json').read_text())
        self.assertEqual(identity['legacy_client_source_commit'], source)

    def test_binaries_and_base_unchanged_and_deterministic(self):
        before = recipe.inventory(self.engine)
        a = self.run_package('a')
        b = self.run_package('b')
        self.assertEqual(a['archive_sha256'], b['archive_sha256'])
        self.assertEqual(before, recipe.inventory(self.engine))
        for name in ('vcmiclient', 'libvcmi.so'):
            self.assertEqual(before[name], recipe.digest(self.root / 'a/New-Horizons-Private-Preview' / name))


if __name__ == '__main__':
    unittest.main()
