# SPDX-License-Identifier: GPL-2.0-or-later
"""Container denial/archive scope; metadata only, no container bodies."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import unittest
from unittest.mock import patch

import credential_content as policy
import credential_scope as scope
import git_credential_blobs as reader
import test_credential_jks_diagnostic as fixtures


class EncDenial(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.JksDiagnostic()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)

    def test_casefold_suffix_is_denied_not_visual_or_text(self):
        for path in ('container.enc', 'folder/CONTAINER.ENC', 'CI/deploy_rsa.eNc'):
            self.assertEqual(scope.classify(path), ('denied', ()))
        self.assertIn('enc', scope.contract()['denied_credential_binary_suffixes'])
        self.assertNotIn('enc', scope.EXCLUDED_SUFFIXES)
        for path in ('enc', 'folder.enc/file.txt', 'container.enc.txt', 'unknown.bin', 'module.def'):
            self.assertEqual(scope.classify(path)[0], 'text')

    def test_regular_and_symlink_modes_refuse_manifest_only_before_fetch(self):
        f = self.fixture
        for mode in (b'100644', b'100755', b'120000'):
            entries = [('cipher.ENC', mode, b'blob', b'e' * 40), f.entries[-1]]
            with f.controls(entries) as (fetch, _, _), patch.object(policy, 'prepare') as detector:
                result = policy.inspect(f.root, revision=f.commit, archive=fixtures.ARCHIVE)
            self.assertEqual(result['status'], 'issue')
            self.assertEqual(result['counts']['denied_entries'], 1)
            self.assertEqual(result['counts']['excluded_entries'], 0)
            self.assertEqual(result['issues'][0]['code'], 'unsupported-relevant-input')
            self.assertEqual(result['issues'][0]['location'], 'manifest-only-no-content-location')
            self.assertEqual(result['issues'][0]['oid'], 'e' * 40)
            fetch.assert_not_called()
            detector.assert_not_called()

    def test_same_oid_text_visual_and_gitlink_aliases_never_trigger_fetch(self):
        f = self.fixture
        for path in ('alias.txt', 'alias.png', 'alias.psd', 'alias.enc'):
            for mode, kind in ((b'100644', b'blob'), (b'120000', b'blob'), (b'160000', b'commit')):
                for reverse in (False, True):
                    entries = [('cipher.enc', b'100644', b'blob', b'e' * 40),
                               (path, mode, kind, b'e' * 40), f.entries[-1]]
                    if reverse:
                        entries.reverse()
                    with f.controls(entries) as (fetch, _, _):
                        result = policy.inspect(f.root, revision=f.commit, archive=fixtures.ARCHIVE)
                    self.assertEqual(result['status'], 'issue')
                    self.assertTrue(any(i['oid'] == 'e' * 40 for i in result['issues']))
                    fetch.assert_not_called()

    def test_jks_opt_in_cannot_bypass_new_container_denial(self):
        f = self.fixture
        entries = f.entries + [('cipher.enc', b'100644', b'blob', b'e' * 40)]
        with f.controls(entries) as (fetch, _, _), patch.object(policy, 'prepare') as detector:
            result = f.inspect()
        f.assert_issues(result)
        self.assertEqual(result['counts']['denied_entries'], 3)
        self.assertEqual(result['evaluation_state'], 'not-started')
        self.assertEqual(len([i for i in result['issues'] if i.get('oid') == 'e' * 40]), 1)
        fetch.assert_not_called()
        detector.assert_not_called()

    def test_jar_is_counted_uninspected_archive_not_inspected_or_denied_text(self):
        f = self.fixture
        for path in ('wrapper.jar', 'platform/WRAPPER.JAR'):
            self.assertEqual(scope.classify(path), ('excluded', ()))
        with f.controls(f.entries + [('wrapper.jar', b'100644', b'blob', b'e' * 40)]) as (fetch, _, _), \
                patch.object(policy, 'witness'), patch.object(policy.payload, 'scan', return_value=({}, {})):
            result = f.inspect()
        f.assert_issues(result)
        self.assertEqual(result['counts']['excluded_by_suffix'], {'jar': 1})
        self.assertEqual(result['counts']['excluded_entries'], 1)
        self.assertEqual(result['counts']['denied_entries'], 2)
        self.assertEqual(result['counts']['evaluated_text_aliases'], 1)
        self.assertEqual(fetch.call_args.args[2], {f.oid.encode()})

    def test_archive_alias_cannot_hide_known_jks_or_visual_identities(self):
        f = self.fixture
        identities = list(fixtures.diagnostic.BINDINGS.values()) + list(scope.KNOWN_BINARY_BINDINGS.values())
        for oid in identities:
            for mode, kind in ((b'100644', b'blob'), (b'120000', b'blob'), (b'160000', b'commit')):
                for reverse in (False, True):
                    entries = f.entries + [('alias.jar', mode, kind, oid.encode())]
                    if reverse:
                        entries.reverse()
                    with f.controls(entries) as (fetch, _, _), patch.object(policy, 'prepare') as detector:
                        result = f.inspect()
                    f.assert_issues(result)
                    self.assertEqual(result['evaluation_state'], 'not-started')
                    fetch.assert_not_called()
                    detector.assert_not_called()
