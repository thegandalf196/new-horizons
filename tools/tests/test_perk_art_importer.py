#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic importer success/fault injection; repository inputs never change."""
import csv
import hashlib
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
import import_new_horizons_perk_art as importer


def snapshot(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob('*') if path.is_file()}


class PerkArtImporterTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / 'repo'
        for folder in ('Mods/new-horizons', 'assets/new-horizons', 'config', 'client/windows', 'docs'):
            (self.repo / folder).mkdir(parents=True)
        (self.repo / 'config/newHorizonsPerks.json').write_text(json.dumps({
            'skills': {'skill': {'perks': [{'id': 'new-horizons:test.perk'}]}}}))
        (self.repo / 'client/windows/NewHorizonsPerkIcons.h').write_text(
            '\tstatic const std::map<std::string, std::string> icons = {\n\t};\n')
        with (self.repo / 'docs/NH_UI_ASSET_INVENTORY.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['Item / ID', 'Art', 'Live binding / source', 'Remaining work / evidence'])
            writer.writeheader()
            writer.writerow({'Item / ID': 'Test (new-horizons:test.perk)', 'Art': 'Not done'})
        inputs = self.root / 'baseline-input'
        inputs.mkdir()
        (inputs / 'base.pcx').write_bytes(b'unchanged synthetic baseline')
        baseline = {'format': 1, 'requiredFamilies': ['builtin-skills-perks-orders-and-ui'],
                    'requiredFamilyCounts': {'builtin-skills-perks-orders-and-ui': 1}, 'entries': [{
                        'resource': 'DATA/base.pcx', 'source': 'base.pcx', 'size': (inputs / 'base.pcx').stat().st_size,
                        'sha256': importer.digest(inputs / 'base.pcx'), 'family': 'builtin-skills-perks-orders-and-ui',
                        'origin': 'synthetic-test', 'selection': 'test', 'approval': 'test'}]}
        manifest = self.repo / 'assets/new-horizons/runtime-art-manifest.json'
        manifest.write_text(json.dumps(baseline))
        archive = self.repo / 'Mods/new-horizons/NewHorizons.nhart'
        nhart.pack(baseline, inputs, archive)
        source_manifest = self.root / 'source-manifest.json'
        source_manifest.write_text('{}')
        review = self.root / 'REVIEW.md'
        review.write_text('Synthetic fixture, not selected art')
        native = []
        for size in (44, 32):
            path = self.root / f'native{size}.png'
            Image.new('RGB', (size, size), (60, 100, 140)).save(path)
            native.append({'size': size, 'sha256': importer.digest(path), 'frozen_copy': str(path)})
        stem = 'NH_perk_test'
        entry = {'id': 'new-horizons:test.perk', 'icon44': stem, 'sourceManifest': str(source_manifest),
                 'reviewManifestSha256': importer.digest(source_manifest), 'review': str(review),
                 'reviewEvidenceSha256': importer.digest(review), 'inputs': native,
                 'origin': 'synthetic-test', 'selection': 'test', 'approval': 'Provisional; no Final approval',
                 'resources': ['SPRITES/' + stem + suffix for suffix in (
                     '.png', '_32.png', '.json', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')]}
        self.plan = self.root / 'plan.json'
        self.plan.write_text(json.dumps({'entries': [entry], 'verifiedCount': 1, 'blocked': [],
                                        'baselineArchiveSha256': importer.digest(archive),
                                        'baselineManifestSha256': importer.digest(manifest),
                                        'handoffSha256': 'a' * 64, 'additiveHandoffSha256': 'b' * 64}))
        self.before = snapshot(self.repo)

    def run_import(self):
        return importer.import_art(self.repo, self.plan, self.root / 'stage', self.root / 'recovery', self.root / 'output')

    def test_success_prepares_ready_external_bundle_without_mutating_input(self):
        result = self.run_import()
        self.assertEqual(result['entryCount'], 8)
        self.assertEqual(result['baselinePreserved'], 1)
        self.assertEqual(snapshot(self.repo), self.before)
        self.assertTrue((self.root / 'output/READY.json').is_file())

    def test_copy_failure_cannot_truncate_repository_archive(self):
        original = importer.shutil.copyfile
        def fail_output_archive(source, destination):
            if str(destination).startswith(str(self.root / 'output')):
                raise OSError('injected output copy failure')
            return original(source, destination)
        with patch.object(importer.shutil, 'copyfile', side_effect=fail_output_archive):
            with self.assertRaisesRegex(OSError, 'injected output copy failure'):
                self.run_import()
        self.assertEqual(snapshot(self.repo), self.before)
        self.assertFalse((self.root / 'output/READY.json').exists())

    def test_metadata_write_failure_leaves_no_ready_receipt_or_source_changes(self):
        original = Path.write_text
        def fail_output_manifest(path, *args, **kwargs):
            if path == self.root / 'output/assets/new-horizons/runtime-art-manifest.json':
                raise OSError('injected output metadata failure')
            return original(path, *args, **kwargs)
        with patch.object(Path, 'write_text', fail_output_manifest):
            with self.assertRaisesRegex(OSError, 'injected output metadata failure'):
                self.run_import()
        self.assertEqual(snapshot(self.repo), self.before)
        self.assertFalse((self.root / 'output/READY.json').exists())


if __name__ == '__main__':
    unittest.main()
