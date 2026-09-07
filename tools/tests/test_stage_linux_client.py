"""Offline integrity/privacy controls for staging, without executing the client."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from stage_linux_client import public_evidence, verify_candidate
from binary_privacy import inspect_bytes


class LinuxStageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        data = json.dumps({'source_commit': 'test'}).encode()
        (self.root / 'BUILD-IDENTITY.json').write_bytes(data)
        (self.root / 'SHA256SUMS').write_text(hashlib.sha256(data).hexdigest() + '  BUILD-IDENTITY.json\n')

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
