#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic early rejection controls; actual source/payload audit is separate."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
spec = importlib.util.spec_from_file_location('overlay', ROOT / 'tools/ci/prepare_frozen_windows_notice_overlay.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class NoticeOverlayTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.original = self.root / 'original'
        self.original.mkdir()
        self.supplement = self.root / 'supplement'
        self.supplement.mkdir()
        self.source = self.original / 'dependency-sources.tar.gz'
        self.source.write_bytes(b'synthetic source identity only; never extracted')
        self.cache = self.root / 'cache.txz'
        self.cache.write_bytes(b'synthetic incorrect cache')
        self.added = [{'reference': r} for r in sorted(overlay.SUPPLEMENTS)]
        self.identity = {'dependency_source_archive': self.source.name,
                         'dependency_source_sha256': hashlib.sha256(self.source.read_bytes()).hexdigest(),
                         'prebuilt_dependencies': {'sha256': '0' * 64}}

    def inputs(self, extra=None):
        with zipfile.ZipFile(self.original / 'New-Horizons-Windows-x64-fixture.zip', 'w') as archive:
            archive.writestr('fixture/BUILD-IDENTITY.json', json.dumps(self.identity))
            archive.writestr('fixture/DEPENDENCIES.json', '[]')
            for name in extra or []:
                archive.writestr(name, 'fixture')
        (self.supplement / 'DEPENDENCIES.json').write_text(json.dumps(self.added))

    def rejected(self, message):
        with self.assertRaisesRegex(RuntimeError, message):
            overlay.prepare(self.original, self.supplement, self.root / 'supplement.tar.gz',
                            self.cache, self.root / 'output')
        self.assertFalse((self.root / 'output').exists())

    def test_missing_static_dependency_is_not_accepted(self):
        self.added.pop()
        self.inputs()
        self.rejected('all three')

    def test_duplicate_static_dependency_is_not_accepted(self):
        self.added[1] = self.added[0]
        self.inputs()
        self.rejected('all three')

    def test_unpinned_newer_dependency_is_not_accepted(self):
        self.added[0] = {'reference': 'brotli/9.0.0#not-the-original-recipe'}
        self.inputs()
        self.rejected('all three')

    def test_wrong_original_source_identity_is_not_accepted(self):
        self.identity['dependency_source_sha256'] = '0' * 64
        self.inputs()
        self.rejected('source identity mismatch')

    def test_wrong_original_prebuilt_cache_is_not_accepted(self):
        self.inputs()
        self.rejected('prebuilt cache identity mismatch')

    def test_unsafe_and_duplicate_zip_paths_are_rejected(self):
        self.inputs(['../escape'])
        self.rejected('Unsafe archive path')
        self.inputs(['fixture/Name', 'fixture/name'])
        self.rejected('Duplicate original ZIP')

    def test_source_path_aliases_and_traversal_are_rejected(self):
        for name in ('../x', '/x', 'x/../y', 'x/./y', 'x//y', 'C:/x', 'x\\y', 'x\0y'):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                overlay.safe_name(name)
        self.assertEqual(overlay.safe_name('component/src/file.c').as_posix(), 'component/src/file.c')
        self.assertEqual(overlay.safe_name('component/').as_posix(), 'component')


if __name__ == '__main__':
    unittest.main()
