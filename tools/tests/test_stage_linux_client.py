"""Offline integrity/privacy controls for staging, without executing the client."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from stage_linux_client import main, public_evidence, verify_candidate
from binary_privacy import inspect_bytes


class LinuxStageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        data = json.dumps({'source_commit': 'test'}).encode()
        (self.root / 'BUILD-IDENTITY.json').write_bytes(data)
        (self.root / 'SHA256SUMS').write_text(hashlib.sha256(data).hexdigest() + '  BUILD-IDENTITY.json\n')

    def test_staging_preserves_engine_identity_without_stale_feature_label(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            arguments = {}
            for key in ('engine-source', 'dependency-source', 'packaging-source'):
                path = work / (key + '.tar.gz')
                path.write_bytes(key.encode())  # Opaque test inputs, not source-audit proof.
                arguments[key] = path
            for key in ('elf-audit', 'inventory', 'build-provenance'):
                path = work / (key + '.json')
                path.write_text('{}')
                arguments[key] = path
            for key in ('notices', 'header-supplement'):
                path = work / key
                path.mkdir()
                arguments[key] = path
            identity = {'source_commit': 'engine-revision',
                        'source_archive_sha256': hashlib.sha256(arguments['engine-source'].read_bytes()).hexdigest()}
            data = json.dumps(identity).encode()
            (self.root / 'BUILD-IDENTITY.json').write_bytes(data)
            (self.root / 'SHA256SUMS').write_text(hashlib.sha256(data).hexdigest() + '  BUILD-IDENTITY.json\n')
            arguments.update(candidate=self.root, output=work / 'stage')
            argv = ['stage_linux_client.py', '--packaging-revision', 'packaging-revision']
            for key, value in arguments.items():
                argv.extend(['--' + key, str(value)])
            with patch.object(sys, 'argv', argv), patch('stage_linux_client.subprocess.check_output', return_value=b'Fixture packaging text\n'):
                main()
            staged = verify_candidate(arguments['output'] / 'New-Horizons-Linux-x64')
            self.assertEqual(staged['source_commit'], identity['source_commit'])
            self.assertEqual(staged['packaging_commit'], 'packaging-revision')
            self.assertEqual(staged['scope'], 'Ubuntu26.04 system-dependent Linux preview; acceptance evidence is separate')
            self.assertEqual(len(staged['source_archives']), 3)
            self.assertEqual((self.root / 'BUILD-IDENTITY.json').read_bytes(), data)

    def test_exact_candidate(self):
        self.assertEqual(verify_candidate(self.root)['source_commit'], 'test')

    def test_extra_file_rejected(self):
        (self.root / 'extra').write_text('not in manifest')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_changed_bytes_rejected(self):
        (self.root / 'BUILD-IDENTITY.json').write_text('{}')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_traversal_rejected(self):
        (self.root / 'SHA256SUMS').write_text('0  ../outside\n')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_symlink_rejected(self):
        try:
            (self.root / 'link').symlink_to(self.root / 'BUILD-IDENTITY.json')
        except OSError:
            self.skipTest('Symlink permission unavailable')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_only_known_candidate_prefix_is_replaced(self):
        data = {str(self.root / 'vcmiclient'): {'NEEDED': str(self.root / 'libvcmi.so')}}
        self.assertEqual(public_evidence(data, self.root), {'bundled/vcmiclient': {'NEEDED': 'bundled/libvcmi.so'}})
        unknown = public_evidence('/home/private/build/library.so', self.root)
        self.assertTrue(inspect_bytes(unknown.encode()))


if __name__ == '__main__':
    unittest.main()
