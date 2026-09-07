#!/usr/bin/env python3
"""Offline controls for source identity, download boundaries and evidence safety."""
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import collect_mingw_sources as sources


def descriptor(name='source.tar.gz'):
    return ('-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA512\n\n'
            'Source: fixture\nVersion: 1\nChecksums-Sha256:\n ' + 'a' * 64 +
            ' 3 ' + name + '\n\n-----BEGIN PGP SIGNATURE-----\nsynthetic\n').encode()


class MinGWSourcesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_descriptor_identity_and_safe_entries(self):
        self.assertEqual(sources.source_entries(descriptor(), 'fixture', '1')[0]['size'], 3)
        with self.assertRaisesRegex(RuntimeError, 'identity'):
            sources.source_entries(descriptor(), 'fixture', '2')
        with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
            sources.source_entries(descriptor('../escape'), 'fixture', '1')

    def test_stale_partial_is_not_deleted(self):
        output = self.root / 'archive'
        partial = self.root / 'archive.part'
        partial.write_bytes(b'previous evidence')
        with patch.object(sources.urllib.request, 'urlopen') as fetch, self.assertRaises(FileExistsError):
            sources.fetch_verified('https://example.invalid/file', output, 'a' * 64, 3)
        fetch.assert_not_called()
        self.assertEqual(partial.read_bytes(), b'previous evidence')

    def test_download_hash_size_and_idempotence(self):
        data = b'abc'
        digest = hashlib.sha256(data).hexdigest()
        output = self.root / 'archive'
        with patch.object(sources.urllib.request, 'urlopen', return_value=io.BytesIO(data)):
            sources.fetch_verified('https://example.invalid/file', output, digest, len(data))
        with patch.object(sources.urllib.request, 'urlopen') as fetch:
            sources.fetch_verified('https://example.invalid/file', output, digest, len(data))
        fetch.assert_not_called()
        self.assertEqual(output.read_bytes(), data)

    def test_bad_download_fails_without_partial_or_output(self):
        output = self.root / 'archive'
        with patch.object(sources.urllib.request, 'urlopen', return_value=io.BytesIO(b'wrong')), self.assertRaises(RuntimeError):
            sources.fetch_verified('https://example.invalid/file', output, 'a' * 64, 3)
        self.assertFalse(output.exists())
        self.assertFalse((self.root / 'archive.part').exists())

    def test_existing_mismatched_evidence_preserved(self):
        output = self.root / 'archive'
        output.write_bytes(b'preserve')
        with self.assertRaisesRegex(RuntimeError, 'Refusing'):
            sources.fetch_verified('https://example.invalid/file', output, 'a' * 64, 3)
        self.assertEqual(output.read_bytes(), b'preserve')

    def test_runtime_catalog_requires_exact_versions(self):
        records = [{'file': name, 'sha256': 'a' * 64, 'distribution_package': sources.GCC_PACKAGE}
                   for name in ('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libssp-0.dll')]
        records.append({'file': 'libwinpthread-1.dll', 'sha256': 'a' * 64, 'distribution_package': sources.PTHREAD_PACKAGE})
        sources.validate_runtime_provenance(records)
        records[0]['distribution_package'] = 'different compiler'
        with self.assertRaisesRegex(RuntimeError, 'provenance'):
            sources.validate_runtime_provenance(records)


if __name__ == '__main__':
    unittest.main()
