#!/usr/bin/env python3
"""Offline fail-closed source-bundle controls for the local package lane."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from package_mingw_client import verify_bundle


class MinGWBundleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'source').write_bytes(b'original')
        self.manifest = {'files': {'source': hashlib.sha256(b'original').hexdigest()}}
        self.save()

    def save(self):
        (self.root / 'BUNDLE-IDENTITY.json').write_text(json.dumps(self.manifest))

    def test_exact_bundle(self):
        self.assertEqual(verify_bundle(self.root), self.manifest)

    def test_changed_file(self):
        (self.root / 'source').write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'Changed'):
            verify_bundle(self.root)

    def test_unlisted_file(self):
        (self.root / 'extra').write_text('not audited')
        with self.assertRaisesRegex(RuntimeError, 'inventory'):
            verify_bundle(self.root)

    def test_link_rejected(self):
        (self.root / 'alias').symlink_to('source')
        with self.assertRaisesRegex(RuntimeError, 'inventory/link'):
            verify_bundle(self.root)

    def test_empty_manifest_rejected(self):
        self.manifest['files'] = {}
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'inventory'):
            verify_bundle(self.root)


if __name__ == '__main__':
    unittest.main()
