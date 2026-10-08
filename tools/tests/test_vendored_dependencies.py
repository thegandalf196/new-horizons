"""Offline recipe preservation gates; no compiler, Conan or network required."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('vendored_dependencies',
    Path(__file__).resolve().parents[1] / 'ci/verify_vendored_dependencies.py')
vendor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vendor)


class VendoredDependencyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.payload = b'# Original synthetic attribution\nrecipe = 1\n'
        (self.root / 'conanfile.py').write_bytes(self.payload)
        (self.root / 'VENDORED.md').write_text('synthetic provenance note')
        self.manifest = {'format': 1, 'origin': vendor.ORIGIN, 'revision': vendor.REVISION,
                         'scope': 'synthetic test', 'licenseStatus': 'original notices retained',
                         'files': {'conanfile.py': {'size': len(self.payload),
                             'sha256': hashlib.sha256(self.payload).hexdigest()}}}
        self.save()
        digest = hashlib.sha256(json.dumps(self.manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        patcher = mock.patch.object(vendor, 'MANIFEST_SHA256', digest)
        patcher.start()
        self.addCleanup(patcher.stop)

    def save(self):
        (self.root / 'UPSTREAM.json').write_text(json.dumps(self.manifest))

    def test_exact_snapshot_without_git_or_remote(self):
        result = vendor.verify(self.root)
        self.assertEqual(result['files'], 1)
        self.assertEqual(result['bytes'], len(self.payload))
        self.assertIn(b'Original synthetic attribution', (self.root / 'conanfile.py').read_bytes())

    def test_changed_source_rejected(self):
        (self.root / 'conanfile.py').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Changed or missing'):
            vendor.verify(self.root)

    def test_missing_source_rejected(self):
        (self.root / 'conanfile.py').unlink()
        with self.assertRaisesRegex(ValueError, 'Changed or missing'):
            vendor.verify(self.root)

    def test_forged_source_and_updated_manifest_not_self_authorizing(self):
        payload = b'changed source and forged inventory'
        (self.root / 'conanfile.py').write_bytes(payload)
        self.manifest['files']['conanfile.py'] = {'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
        self.save()
        with self.assertRaisesRegex(ValueError, 'fingerprint'):
            vendor.verify(self.root)

    def test_unlisted_source_rejected(self):
        (self.root / 'new-recipe.py').write_bytes(b'unreviewed')
        with self.assertRaisesRegex(ValueError, 'inventory'):
            vendor.verify(self.root)

    def test_source_symlink_rejected(self):
        (self.root / 'conanfile.py').unlink()
        original = self.root / 'original'
        original.write_bytes(self.payload)
        (self.root / 'conanfile.py').symlink_to(original)
        with self.assertRaisesRegex(ValueError, 'Linked'):
            vendor.verify(self.root)

    def test_provenance_symlink_rejected(self):
        original = self.root / 'outside-provenance'
        original.write_bytes((self.root / 'UPSTREAM.json').read_bytes())
        (self.root / 'UPSTREAM.json').unlink()
        (self.root / 'UPSTREAM.json').symlink_to(original)
        with self.assertRaisesRegex(ValueError, 'provenance'):
            vendor.verify(self.root)

    def test_duplicate_metadata_key_rejected(self):
        (self.root / 'UPSTREAM.json').write_text('{"format":1,"format":1}')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            vendor.verify(self.root)


class ActualPreservedDependencyTest(unittest.TestCase):
    def test_pinned_upstream_tree_is_intact_and_local(self):
        root = Path(__file__).resolve().parents[2]
        result = vendor.verify(root / 'dependencies')
        self.assertEqual(result, {'revision': vendor.REVISION, 'files': 145, 'bytes': 4945402})
        self.assertNotIn('[submodule "dependencies"]', (root / '.gitmodules').read_text())
        self.assertIn('from dependencies.conanfile import VCMI', (root / 'conanfile.py').read_text())


if __name__ == '__main__':
    unittest.main()
