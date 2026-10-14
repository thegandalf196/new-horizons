"""Synthetic spell importer success and failure safety; no private dependencies."""
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nhart
import import_new_horizons_spell_art as importer
from .test_perk_art_importer import snapshot


class SpellArtImporterTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name); self.repo = self.root / 'repo'
        for folder in ('Mods/new-horizons/Content/config/spells', 'assets/new-horizons/art-source/spell-symbols-v2', 'docs'):
            (self.repo / folder).mkdir(parents=True)
        graphics = {role: 'placeholder' for role in importer.ROLES}
        config = {key: {'name': key, 'graphics': graphics, 'level': 2, 'active': True}
                  for key in sorted(importer.SPELLS | {'holyWrath', 'regeneration'})}
        config_text = json.dumps(config, indent='\t')
        (self.repo / importer.CONFIG).write_text(config_text)
        (self.repo / 'docs/NH_UI_ASSET_INVENTORY.csv').write_text(
            'Area,Item / ID,Live binding / source,Implementation,Art,Remaining work / evidence\n')
        legacy = {'families': [{'spellKey': key, 'liveBindings': graphics} for key in ('holyWrath', 'regeneration')]}
        (self.repo / 'assets/new-horizons/art-source/spell-symbols-v2/manifest.json').write_text(json.dumps(legacy, indent=2))
        inputs = self.root / 'input'; images = inputs / 'Mods/new-horizons/Images'; images.mkdir(parents=True)
        entries = []
        for key in ('holyWrath', 'regeneration'):
            filename = 'NH_' + key + '_32.png'; path = images / filename
            Image.new('RGBA', (32, 32), (100, 100, 100, 255)).save(path)
            entries.append({'resource': 'SPRITES/' + filename, 'source': 'Mods/new-horizons/Images/' + filename,
                'size': path.stat().st_size, 'sha256': importer.digest(path),
                'family': 'builtin-skills-perks-orders-and-ui', 'origin': 'synthetic', 'selection': 'test', 'approval': 'test'})
        manifest = {'format': 1, 'entries': entries, 'requiredFamilies': ['builtin-skills-perks-orders-and-ui'],
                    'requiredFamilyCounts': {'builtin-skills-perks-orders-and-ui': 2}}
        (self.repo / importer.MANIFEST).write_text(json.dumps(manifest))
        nhart.pack(manifest, inputs, self.repo / importer.ARCHIVE)
        artist = self.root / 'artist'; artist.mkdir()
        assets, hashes = [], {}
        for index, key in enumerate(sorted(importer.SPELLS)):
            # Resource stems are intentionally independent of gameplay IDs.
            stem = 'NH_spell_' + chr(97 + index)
            roles = {}
            for role, size in importer.ROLES.items():
                filename = stem + '-' + str(size) + '.png'; path = artist / filename
                image = Image.new('RGBA', (size, size), (100, 120, 140, 255)); image.putpixel((0, 0), (0, 0, 0, 0)); image.save(path)
                roles[role] = filename; hashes[filename] = importer.digest(path)
            refs = {}
            for role in ('master', 'prompt', 'preview'):
                filename = stem + '-' + role + '.txt'; path = artist / filename
                path.write_text('Synthetic fixture, not artwork'); hashes[filename] = importer.digest(path); refs[role] = filename
            assets.append({'spell': 'new-horizons:' + key, 'proposedRoles': roles, **refs})
        self.handoff = artist / 'HANDOFF.json'; self.handoff.write_text(json.dumps({'assets': assets, 'sha256': hashes}))
        self.before = snapshot(self.repo); self.artist_before = snapshot(artist)

    def run_import(self):
        return importer.prepare(self.repo, [self.handoff], self.root / 'stage', self.root / 'recovery', self.root / 'output')

    def assert_unchanged(self):
        self.assertEqual(snapshot(self.repo), self.before)
        self.assertEqual(snapshot(self.handoff.parent), self.artist_before)

    def test_external_success_and_graphics_only(self):
        result = self.run_import(); self.assertEqual(result['entryCount'], 53); self.assertEqual(result['spellCount'], 17)
        self.assertTrue((self.root / 'output/READY.json').exists()); self.assert_unchanged()
        original = importer.parse_jsonc((self.repo / importer.CONFIG).read_text())
        updated = importer.parse_jsonc((self.root / 'output' / importer.CONFIG).read_text())
        self.assertEqual(importer.non_graphics_digest(original), importer.non_graphics_digest(updated))

    def test_output_archive_copy_failure_preserves_all_inputs(self):
        original = importer.shutil.copyfile
        def fail(source, target):
            if target == self.root / 'output' / importer.ARCHIVE:
                raise OSError('injected archive failure')
            return original(source, target)
        with patch.object(importer.shutil, 'copyfile', side_effect=fail):
            with self.assertRaises(OSError): self.run_import()
        self.assert_unchanged(); self.assertFalse((self.root / 'output/READY.json').exists())

    def test_output_metadata_failure_preserves_all_inputs(self):
        original = Path.write_text
        def fail(path, *args, **kwargs):
            if path == self.root / 'output' / importer.MANIFEST:
                raise OSError('injected metadata failure')
            return original(path, *args, **kwargs)
        with patch.object(Path, 'write_text', fail):
            with self.assertRaises(OSError): self.run_import()
        self.assert_unchanged(); self.assertFalse((self.root / 'output/READY.json').exists())

    def test_incomplete_handoff_stops_before_output(self):
        evidence = json.loads(self.handoff.read_text()); evidence['assets'].pop()
        self.handoff.write_text(json.dumps(evidence))
        with self.assertRaisesRegex(ValueError, 'Incomplete'): self.run_import()
        self.assertEqual(snapshot(self.repo), self.before); self.assertFalse((self.root / 'output').exists())

    def test_output_inside_checkout_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'external'):
            importer.prepare(self.repo, [self.handoff], self.root / 'stage', self.root / 'recovery', self.repo / 'output')
        self.assert_unchanged()


if __name__ == '__main__':
    unittest.main()
