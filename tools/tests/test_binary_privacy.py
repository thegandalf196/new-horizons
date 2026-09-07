#!/usr/bin/env python3
from pathlib import Path
import sys
import tempfile
import unittest
import json
import subprocess
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from binary_privacy import inspect_bytes, require_clean, verified_github_ci_provenance, audit_directory


class BinaryPrivacyTest(unittest.TestCase):
    def test_ascii_data_section_paths_are_not_treated_as_debug_only(self):
        data = b'MZsynthetic\0.rdata\0/home/synthetic-builder/source.cpp\0'
        hits = inspect_bytes(data)
        self.assertTrue(any(h['category'] == 'unix-home' for h in hits))
        self.assertNotIn('synthetic-builder', str(hits))

    def test_utf16_at_both_alignments_and_supplementary_prefix(self):
        text = '\U0001f600 C:\\Users\\synthetic-builder\\source.cpp'
        for prefix in (b'', b'x'):
            data = prefix + text.encode('utf-16-le')
            hits = inspect_bytes(data)
            wanted = len(prefix) + len('\U0001f600 '.encode('utf-16-le'))
            self.assertTrue(any(h['category'] == 'windows-profile' and h['offset'] == wanted for h in hits))

    def test_hosted_workspace_and_relative_source_paths_are_not_profiles(self):
        self.assertEqual(inspect_bytes(b'D:\\a\\project\\source.cpp\0./lib/source.cpp\0'), [])

    def test_ci_profile_requires_explicit_provenance_and_exact_cache_subtree(self):
        valid = b'C:/users/runneradmin/.conan2/p/b/package/source.cpp'
        self.assertEqual(inspect_bytes(valid)[0]['classification'], 'unapproved-profile-path')
        self.assertEqual(inspect_bytes(valid, True)[0]['classification'], 'ci-service-build-path')
        self.assertEqual(inspect_bytes(valid + b'/../header.h', True)[0]['classification'], 'ci-service-build-path')
        for invalid in (b'C:/users/other/.conan2/p/a', b'C:/users/runneradmin/Documents/a',
                        b'C:/users/runneradmin/.conan2/p/../../private', b'D:/users/runneradmin/.conan2/p/a',
                        b'/home/synthetic-builder/.cache/source.cpp'):
            self.assertTrue(all(h['classification'] == 'unapproved-profile-path' for h in inspect_bytes(invalid, True)))

    def test_ci_escape_after_whitespace_or_quote_is_not_truncated(self):
        for component in ('cache with space', "cache'withquote", 'cache\twithtab'):
            text = 'C:/Users/runneradmin/.conan2/p/' + component + '/../../../../Alice/private.cpp'
            for encoding in ('ascii', 'utf-16-le'):
                for prefix in (b'', b'x'):
                    hits = inspect_bytes(prefix + text.encode(encoding), True)
                    self.assertTrue(hits)
                    self.assertTrue(all(h['classification'] == 'unapproved-profile-path' for h in hits))

    def test_ci_classification_retains_offsets_and_binary_hash_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'sample.dll').write_bytes(b'MZ\x00C:/users/runneradmin/.conan2/p/a.cpp\x00')
            require_clean(root, {'synthetic': 'explicit tested provenance boundary'})
            report = json.loads((root / 'BINARY-PRIVACY.json').read_text())
            self.assertEqual(len(report['reported_profile_paths']['sample.dll']), 1)
            self.assertEqual(report['reported_profile_paths']['sample.dll'][0]['offset'], 3)
            self.assertEqual(len(report['binary_sha256']['sample.dll']), 64)

    def test_missing_or_self_hosted_ci_context_is_not_provenance(self):
        with patch.dict('os.environ', {}, clear=True):
            self.assertIsNone(verified_github_ci_provenance())
        with patch.dict('os.environ', {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'self-hosted'}, clear=True):
            self.assertIsNone(verified_github_ci_provenance())

    def test_exact_hosted_context_requires_every_provenance_field(self):
        env = {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'github-hosted', 'RUNNER_OS': 'Windows',
               'GITHUB_REPOSITORY': 'thegandalf196/vcmi', 'GITHUB_RUN_ID': '123456', 'GITHUB_SHA': 'a' * 40,
               'NH_VERIFIED_CONAN_CACHE_SHA256': '6772d2e9f0a730329a195edce895a863d9115c4bba43101a9a6df3fefe6cfe43'}
        with patch.dict('os.environ', env, clear=True):
            self.assertEqual(verified_github_ci_provenance()['run_id'], '123456')
        for missing in env:
            partial = dict(env)
            del partial[missing]
            with patch.dict('os.environ', partial, clear=True):
                self.assertIsNone(verified_github_ci_provenance())

    def test_elf_executable_and_versioned_library_are_not_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('vcmiclient', 'libvcmi.so.1'):
                (root / name).write_bytes(b'\x7fELF\x00/home/synthetic-builder/install/bin\x00')
            (root / 'source.cpp').write_bytes(b'/home/synthetic-builder/fixture-only')
            self.assertEqual(set(audit_directory(root)), {'vcmiclient', 'libvcmi.so.1'})
            with self.assertRaisesRegex(RuntimeError, 'do not publish'):
                require_clean(root)
            for name in ('vcmiclient', 'libvcmi.so.1'):
                (root / name).write_bytes(b'\x7fELF\x00/usr/local/bin\x00')
            self.assertEqual(audit_directory(root), {})

    def test_missing_or_non_directory_root_fails_closed_with_sanitized_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            regular_file = root / 'not-a-directory'
            regular_file.write_bytes(b'fixture')
            for invalid in (root / 'missing', regular_file):
                with self.assertRaisesRegex(RuntimeError, 'audit root'):
                    require_clean(invalid)
                process = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'ci/binary_privacy.py'),
                                          '--directory', str(invalid)], capture_output=True, text=True)
                self.assertEqual(process.returncode, 1)
                report = json.loads(process.stdout)
                self.assertFalse(report['pass'])
                self.assertEqual(report['error'], 'invalid-or-unreadable-audit-input')
                self.assertNotIn(str(root), process.stdout + process.stderr)
                self.assertEqual(process.stderr, '')

    def test_msys_profiles_require_provenance_and_reject_escapes(self):
        for text in ('/c/users/runneradmin/.conan2/p/b/package/source.cpp',
                     '/C/Users/runneradmin/.conan2/p/b/package/source.cpp'):
            for encoding in ('ascii', 'utf-16-le'):
                data = b'x' + text.encode(encoding)
                hits = inspect_bytes(data)
                self.assertEqual(len(hits), 1)
                self.assertEqual(hits[0]['classification'], 'unapproved-profile-path')
                self.assertEqual(inspect_bytes(data, True)[0]['classification'], 'ci-service-build-path')
        for text in ('/c/users/other/.conan2/p/a', '/d/users/runneradmin/.conan2/p/a',
                     '/c/users/runneradmin/Documents/a',
                     '/c/users/runneradmin/.conan2/p/cache with space/../../../../private',
                     '/Users/runneradmin/.conan2/p/a'):
            for encoding in ('ascii', 'utf-16-le'):
                hits = inspect_bytes(b'x' + text.encode(encoding), True)
                self.assertTrue(hits)
                self.assertTrue(all(hit['classification'] == 'unapproved-profile-path' for hit in hits))

    def test_binary_gate_rejects_runtime_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'sample.dll').write_bytes(b'MZ\0/home/synthetic-builder/a.cpp\0')
            with self.assertRaisesRegex(RuntimeError, 'do not publish'):
                require_clean(root)
            with self.assertRaisesRegex(RuntimeError, 'do not publish'):
                require_clean(root, {'synthetic': 'CI approval cannot waive another profile'})


if __name__ == '__main__':
    unittest.main()
