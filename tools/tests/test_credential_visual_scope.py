# SPDX-License-Identifier: GPL-2.0-or-later
"""Metadata-only visual bindings; all opened bodies below are invented text."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import hashlib
import json
import time
import unittest
from unittest.mock import patch

import credential_content as policy
import credential_scope as scope
import git_credential_blobs as reader
import test_credential_jks_diagnostic as fixtures
from scanner_runtime import ScannerError


class VisualScope(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.JksDiagnostic()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.visuals = [(p, b'100644', b'blob', oid.encode()) for p, oid in scope.KNOWN_BINARY_BINDINGS.items()]

    def test_psd_icns_only_explicit_suffixes_and_linker_def_stays_text(self):
        for path in ('drawing.psd', 'icon.ICNS'):
            self.assertEqual(scope.classify(path)[0], 'excluded')
        for path in ('library.def', 'unknown.bin', 'unknown'):
            self.assertEqual(scope.classify(path)[0], 'text')
        self.assertEqual(scope.classify('secret.enc'), ('denied', ()))
        self.assertNotIn('def', scope.EXCLUDED_SUFFIXES)
        self.assertNotIn('bin', scope.EXCLUDED_SUFFIXES)

    def test_exact_visuals_and_suffixes_count_separately_without_fetching_them(self):
        f = self.fixture
        entries = f.entries + self.visuals + [('drawing.psd', b'100644', b'blob', b'c' * 40),
                                             ('icon.icns', b'100644', b'blob', b'd' * 40)]
        with f.controls(entries) as (fetch, _, _), patch.object(policy, 'witness'), \
                patch.object(policy.payload, 'scan', return_value=({}, {})):
            result = f.inspect()
        f.assert_issues(result)
        self.assertEqual(result['text_status'], 'clean')
        self.assertEqual(result['counts']['known_binary_entries'], 4)
        self.assertEqual(result['counts']['known_binary_objects'], 4)
        self.assertEqual(result['counts']['excluded_entries'], 2)
        self.assertEqual(result['counts']['excluded_by_suffix'], {'psd': 1, 'icns': 1})
        self.assertEqual(result['counts']['evaluated_unique_objects'], 1)
        self.assertEqual(fetch.call_args.args[2], {f.oid.encode()})
        self.assertTrue(all(not row['inspected'] for row in result['scope']['known_binary_bindings']))

    def test_replaced_mode_kind_renamed_and_duplicate_visuals_refuse_before_fetch(self):
        f = self.fixture
        for primary in self.visuals:
            p, mode, kind, oid = primary
            mutations = [[(p, mode, kind, b'f' * 40)], [(p, b'100755', kind, oid)],
                         [(p, b'120000', kind, oid)], [(p, b'160000', b'commit', oid)],
                         [('renamed.txt', mode, kind, oid)], [primary, primary]]
            for entries in mutations:
                for reverse in (False, True):
                    cohort = f.entries + entries
                    if reverse:
                        cohort.reverse()
                    with f.controls(cohort) as (fetch, _, _), patch.object(policy, 'prepare') as detector:
                        result = f.inspect()
                    f.assert_issues(result)
                    self.assertTrue(any(i['code'] == 'known-binary-binding-mismatch' for i in result['issues']))
                    fetch.assert_not_called()
                    detector.assert_not_called()

    def test_known_binary_oid_alias_dominates_suffix_and_gitlink_in_both_orders(self):
        f = self.fixture
        for primary in self.visuals:
            for alias in ('alias.txt', 'alias.png', 'alias.psd', 'alias.def'):
                for mode, kind in ((b'100644', b'blob'), (b'120000', b'blob'), (b'160000', b'commit')):
                    for reverse in (False, True):
                        entries = f.entries + [primary, (alias, mode, kind, primary[3])]
                        if reverse:
                            entries.reverse()
                        with f.controls(entries) as (fetch, _, _):
                            result = f.inspect()
                        f.assert_issues(result)
                        fetch.assert_not_called()

    def test_absent_visuals_do_not_create_exemptions_or_required_content_reads(self):
        f = self.fixture
        with f.controls() as (fetch, _, _), patch.object(policy, 'prepare', side_effect=OSError('private')):
            result = f.inspect()
        f.assert_issues(result)
        self.assertEqual(result['counts']['known_binary_entries'], 0)
        self.assertEqual(fetch.call_args.args[2], {f.oid.encode()})

    def read_bad(self, data, expected):
        oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest().encode()
        with patch.object(reader, 'git', return_value=data):
            with self.assertRaises(reader.SelectedTextError) as error:
                reader.read_object(self.fixture.root, 'sha1', oid, len(data), time.monotonic() + 5)
        self.assertEqual(str(error.exception), expected)
        self.assertEqual(error.exception.object_oid, oid.decode())
        return error.exception

    def test_nul_and_invalid_utf8_return_only_verified_oid_and_fixed_code(self):
        f = self.fixture
        for data, code in ((b'private\0hidden', 'nul-in-selected-text'),
                           (b'private\xff', 'selected-text-not-UTF8'), (b'private\xc3', 'selected-text-not-UTF8')):
            error = self.read_bad(data, code)
            with f.controls(), patch.object(reader, 'fetch_objects', side_effect=error):
                result = f.inspect()
            f.assert_issues(result)
            self.assertEqual(result['issues'][-1], {'code': code, 'object_oid': error.object_oid})
            self.assertEqual(result['text_status'], 'failed')
            self.assertEqual(result['findings'], [])
            self.assertNotIn('private', json.dumps(result))

    def test_unverified_hash_failure_does_not_claim_object_format_identity(self):
        with patch.object(reader, 'git', return_value=b'not requested'):
            with self.assertRaises(ScannerError) as error:
                reader.read_object(self.fixture.root, 'sha1', b'a' * 40, 13, time.monotonic() + 5)
        self.assertEqual(str(error.exception), 'selected-object-size-or-hash-mismatch')
        self.assertFalse(hasattr(error.exception, 'object_oid'))

    def test_strict_default_keeps_jks_denials_with_visual_metadata_only(self):
        f = self.fixture
        with f.controls(f.entries + self.visuals) as (fetch, metadata, _), patch.object(policy, 'prepare') as detector:
            result = policy.inspect(f.root, revision=f.commit, archive=fixtures.ARCHIVE)
        self.assertEqual(result['status'], 'issue')
        self.assertEqual(len(result['issues']), 2)
        self.assertNotIn('gate_pass', result)
        self.assertEqual(result['counts']['known_binary_entries'], 4)
        fetch.assert_not_called()
        metadata.assert_not_called()
        detector.assert_not_called()
