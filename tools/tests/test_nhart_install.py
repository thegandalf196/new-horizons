"""Synthetic real-container final-install contract; no original artwork needed."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_new_horizons_art_install import verify_install, nhart, RUNTIME_ART_PACK
from package_private_preview import package as package_private_preview


class NHArtInstallTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.resources = self.root / 'install'
        self.inputs = self.root / 'selected'
        for tree in ('config', 'scripts', 'Mods/vcmi', 'Mods/new-horizons'):
            (self.resources / tree).mkdir(parents=True)
        self.expected = {'format': 1, 'requiredFamilies': ['fixture'],
                         'requiredFamilyCounts': {'fixture': 2}, 'entries': []}
        for source, resource, payload in (
            ('Mods/new-horizons/Images/unit.png', 'SPRITES/unit.png', b'synthetic art'),
            ('config/newHorizonsMagicAssets.json', 'CONFIG/newHorizonsMagicAssets.json', b'{}'),
        ):
            target = self.inputs / source
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
            self.expected['entries'].append({'source': source, 'resource': resource,
                'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
                'family': 'fixture', 'origin': 'synthetic test', 'selection': 'test selection',
                'approval': 'test only'})
        self.manifest = self.root / 'expected.json'
        self.manifest.write_text(json.dumps(self.expected))
        nhart.pack(self.expected, self.inputs, self.resources / RUNTIME_ART_PACK)
        self.builtin = self.resources / 'config/filesystem.json'
        self.mod = self.resources / 'Mods/new-horizons/mod.json'
        self.builtin.write_text('// synthetic bootstrap\n' + json.dumps({'filesystem': {
            '': [{'type': 'nhart', 'path': RUNTIME_ART_PACK, 'overlay': True}],
            'DATA/': [{'type': 'lod', 'path': 'Data/H3bitmap.lod'}]}}))
        self.mod.write_text(json.dumps({'filesystem': {'': [
            {'type': 'dir', 'path': '/Content'}, {'type': 'nhart', 'path': '/NewHorizons.nhart'}]}}))

    def verify(self):
        return verify_install(self.resources, self.manifest)

    def test_complete_final_install_and_cli(self):
        self.assertEqual(self.verify()['selectedEntries'], 2)
        script = Path(__file__).resolve().parents[1] / 'verify_new_horizons_art_install.py'
        result = subprocess.run([sys.executable, str(script), '--resources', str(self.resources),
                                 '--manifest', str(self.manifest)], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('no loose duplicates, both mounts', result.stdout)

    def test_missing_pack_rejected(self):
        (self.resources / RUNTIME_ART_PACK).unlink()
        with self.assertRaisesRegex(RuntimeError, 'pack missing'):
            self.verify()

    def test_historical_preview_rejects_pack_before_any_overlay(self):
        output = self.root / 'must-not-create'
        with self.assertRaisesRegex(ValueError, 'cannot overlay an NHART engine'):
            package_private_preview(self.resources, self.inputs, self.manifest,
                                    'unused', 'synthetic', 'linux', output)
        self.assertFalse(output.exists())

    def test_valid_but_wrong_expected_pack_rejected(self):
        wrong = copy.deepcopy(self.expected)
        wrong['entries'][0]['approval'] = 'different selected source'
        nhart.pack(wrong, self.inputs, self.resources / RUNTIME_ART_PACK)
        with self.assertRaisesRegex(RuntimeError, 'expected source manifest'):
            self.verify()

    def test_declared_loose_duplicates_rejected_including_config(self):
        for entry in self.expected['entries']:
            with self.subTest(source=entry['source']):
                duplicate = self.resources / entry['source']
                duplicate.parent.mkdir(parents=True, exist_ok=True)
                duplicate.write_bytes((self.inputs / entry['source']).read_bytes())
                with self.assertRaisesRegex(RuntimeError, 'still installed loose'):
                    self.verify()
                duplicate.unlink()

    def test_bootstrap_mount_must_be_regular(self):
        target = self.root / 'bootstrap-copy.json'
        target.write_bytes(self.builtin.read_bytes())
        self.builtin.unlink()
        self.builtin.symlink_to(target)
        with self.assertRaises(RuntimeError):
            self.verify()

    def test_builtin_mount_requires_explicit_overlay(self):
        for overlay in (None, False, 1):
            with self.subTest(overlay=overlay):
                row = {'type': 'nhart', 'path': RUNTIME_ART_PACK}
                if overlay is not None:
                    row['overlay'] = overlay
                self.builtin.write_text(json.dumps({'filesystem': {'': [row]}}))
                with self.assertRaisesRegex(RuntimeError, 'NHART mount'):
                    self.verify()

    def test_each_mount_scope_and_path_required(self):
        for config, wrong in ((self.builtin, '/NewHorizons.nhart'),
                              (self.mod, RUNTIME_ART_PACK)):
            with self.subTest(config=config.name):
                original = config.read_bytes()
                config.write_text(json.dumps({'filesystem': {'': [{'type': 'nhart', 'path': wrong}]}}))
                with self.assertRaisesRegex(RuntimeError, 'NHART mount'):
                    self.verify()
                config.write_bytes(original)

    def test_mount_cannot_be_shadowed_or_duplicated(self):
        for rows in ([{'type': 'nhart', 'path': '/NewHorizons.nhart'}, {'type': 'dir', 'path': '/Images'}],
                     [{'type': 'nhart', 'path': '/NewHorizons.nhart'}] * 2):
            with self.subTest(rows=rows):
                self.mod.write_text(json.dumps({'filesystem': {'': rows}}))
                with self.assertRaisesRegex(RuntimeError, 'ambiguous NHART mount'):
                    self.verify()


if __name__ == '__main__':
    unittest.main()
