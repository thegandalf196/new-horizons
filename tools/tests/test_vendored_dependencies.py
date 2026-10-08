"""Offline recipe preservation gates; no compiler, Conan or network required."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('vendored_dependencies',
    Path(__file__).resolve().parents[1] / 'ci/verify_vendored_dependencies.py')
vendor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vendor)


def windows_checkout(root, payload, attributes=None):
    """Exercise real Git conversion in disposable repos, never the worktree."""
    source = root / 'source'
    source.mkdir()
    for name, data in payload.items():
        path = source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    if attributes is not None:
        (source / '.gitattributes').write_bytes(attributes)
    environment = dict(os.environ, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull)
    def git(*args, cwd=source):
        return subprocess.check_output(['git', *args], cwd=cwd, env=environment,
                                       stderr=subprocess.STDOUT)
    git('init', '--quiet')
    git('config', 'core.autocrlf', 'false')
    git('add', '--all')
    git('-c', 'user.name=Synthetic fixture', '-c', 'user.email=fixture@example.invalid',
        'commit', '--quiet', '-m', 'Private checkout fixture')
    checkout = root / 'checkout'
    git('clone', '--quiet', '--no-local', '--config', 'core.autocrlf=true',
        str(source), str(checkout))
    return checkout


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

    def test_windows_checkout_preserves_mixed_raw_line_endings(self):
        upstream = {'lf.py': b'# LF attribution\nvalue = 1\n',
                    'crlf.bat': b'@rem CRLF attribution\r\n@echo original\r\n'}
        self.manifest['files'] = {name: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                                  for name, data in upstream.items()}
        self.save()
        digest = hashlib.sha256(json.dumps(self.manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        payload = {'dependencies/' + name: data for name, data in upstream.items()}
        payload['dependencies/UPSTREAM.json'] = (self.root / 'UPSTREAM.json').read_bytes()
        payload['dependencies/VENDORED.md'] = b'Synthetic provenance\n'
        payload['ordinary.txt'] = b'ordinary platform text\n'
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(vendor, 'MANIFEST_SHA256', digest):
            checkout = windows_checkout(Path(temporary), payload,
                (Path(__file__).resolve().parents[2] / '.gitattributes').read_bytes())
            result = vendor.verify(checkout / 'dependencies')
            self.assertEqual(result['files'], 2)
            for name, data in upstream.items():
                self.assertEqual((checkout / 'dependencies' / name).read_bytes(), data)
            self.assertEqual((checkout / 'ordinary.txt').read_bytes(), b'ordinary platform text\r\n')


class ActualPreservedDependencyTest(unittest.TestCase):
    def test_pinned_upstream_tree_is_intact_and_local(self):
        root = Path(__file__).resolve().parents[2]
        result = vendor.verify(root / 'dependencies')
        self.assertEqual(result, {'revision': vendor.REVISION, 'files': 145, 'bytes': 4945402})
        self.assertNotIn('[submodule "dependencies"]', (root / '.gitmodules').read_text())
        self.assertIn('from dependencies.conanfile import VCMI', (root / 'conanfile.py').read_text())

    def test_actual_inventory_survives_windows_autocrlf_checkout(self):
        root = Path(__file__).resolve().parents[2]
        inventory = json.loads((root / 'dependencies/UPSTREAM.json').read_text())['files']
        payload = {'dependencies/' + name: (root / 'dependencies' / name).read_bytes()
                   for name in (*inventory, 'UPSTREAM.json', 'VENDORED.md')}
        # The unprotected control reproduces the runner's first failing file.
        with tempfile.TemporaryDirectory() as temporary:
            checkout = windows_checkout(Path(temporary), payload)
            with self.assertRaisesRegex(ValueError, r'Changed or missing dependency source: \.github/workflows/rebuildDependencies.yml'):
                vendor.verify(checkout / 'dependencies')
        with tempfile.TemporaryDirectory() as temporary:
            checkout = windows_checkout(Path(temporary), payload, (root / '.gitattributes').read_bytes())
            self.assertEqual(vendor.verify(checkout / 'dependencies'),
                             {'revision': vendor.REVISION, 'files': 145, 'bytes': 4945402})
            for name in inventory:
                self.assertEqual((checkout / 'dependencies' / name).read_bytes(), payload['dependencies/' + name])


if __name__ == '__main__':
    unittest.main()
