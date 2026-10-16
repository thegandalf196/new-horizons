# SPDX-License-Identifier: GPL-2.0-or-later
"""Inert text and exact manifest identities only; never read any keystore blob."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import credential_content as policy
import credential_jks_diagnostic as diagnostic
import credential_payload as payload
import git_credential_blobs as reader
from scanner_runtime import ScannerError

ARCHIVE = Path(os.environ['NH_CREDENTIAL_TEST_ARCHIVE'])


class JksDiagnostic(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        data = b'ordinary public synthetic text\n'
        self.oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        self.objects = {self.oid: reader.VerifiedObject(self.oid, data, hashlib.sha256(data).hexdigest(), 2, len(data))}
        self.entries = [(p, b'100644', b'blob', oid.encode()) for p, oid in diagnostic.BINDINGS.items()]
        self.entries += [('public.txt', b'100644', b'blob', self.oid.encode())]
        self.manifest = b'synthetic complete manifest'
        self.commit = 'a' * 40
        self.original = b''.join(b'100644 blob ' + diagnostic.BINDINGS[p].encode() + b'\t' + p.encode() + b'\0'
                                 for p in sorted(diagnostic.BINDINGS))

    def selection(self, entries=None, commit='default'):
        return (self.root, 'sha1', self.commit if commit == 'default' else commit,
                self.manifest, self.entries if entries is None else entries)

    def provenance(self, root, *args, **kwargs):
        if args[0] == 'merge-base':
            return b''
        if args[0] == 'rev-parse':
            return (self.commit + '\n').encode()
        self.assertEqual(args[:4], ('ls-tree', '-rz', '--full-tree', diagnostic.ANCESTOR))
        return self.original

    @contextlib.contextmanager
    def controls(self, entries=None, commit='default'):
        with patch.object(reader, 'select', return_value=self.selection(entries, commit)), \
                patch.object(reader, 'recheck') as recheck, \
                patch.object(reader, 'fetch_objects', return_value=self.objects) as fetch, \
                patch.object(diagnostic, 'git', side_effect=self.provenance) as metadata:
            yield fetch, metadata, recheck

    def inspect(self, **kwargs):
        return policy.inspect(self.root, revision=self.commit, archive=ARCHIVE,
                              diagnostic_inherited_jks=True, **kwargs)

    def assert_issues(self, result):
        self.assertEqual(result['status'], 'issue')
        self.assertFalse(result['gate_pass'])
        self.assertEqual(result['selection_sha256'], hashlib.sha256(self.manifest).hexdigest())
        paths = {hashlib.sha256(p.encode()).hexdigest() for p in diagnostic.BINDINGS}
        known = [i for i in result['issues'] if i.get('path_sha256') in paths]
        self.assertEqual(len(known), 2)
        self.assertTrue(all(i['code'] == 'unsupported-relevant-input' for i in known))
        self.assertNotIn('CI/android', json.dumps(result))

    def test_exact_opt_in_scans_only_supported_text_and_never_passes_gate(self):
        with self.controls() as (fetch, metadata, recheck):
            result = self.inspect()
        self.assert_issues(result)
        self.assertEqual(result['evaluation_state'], 'complete')
        self.assertEqual(result['text_status'], 'clean')
        self.assertEqual(result['counts']['evaluated_text_aliases'], 1)
        self.assertEqual(result['counts']['uninspected_binary_objects'], 2)
        self.assertEqual(len(result['uninspected']), 2)
        self.assertTrue(all(not row['inspected'] for row in result['uninspected']))
        self.assertEqual(fetch.call_args.args[2], {self.oid.encode()})
        self.assertTrue(all(call.args[1] != 'cat-file' for call in metadata.call_args_list))
        self.assertEqual(recheck.call_count, 2)

    def test_strict_default_still_refuses_without_fetch_or_detector(self):
        with self.controls() as (fetch, metadata, _), \
                patch.object(policy, 'prepare', side_effect=AssertionError('no detector')):
            result = policy.inspect(self.root, revision=self.commit, archive=ARCHIVE)
        self.assertEqual(result['status'], 'issue')
        self.assertEqual(result['evaluation_state'], 'not-started')
        self.assertNotIn('gate_pass', result)
        fetch.assert_not_called()
        metadata.assert_not_called()

    def test_supported_text_finding_is_retained_without_clearing_binary_denials(self):
        data = ('token="' + policy.synthetic_token() + '"\n').encode()
        oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        self.objects = {oid: reader.VerifiedObject(oid, data, hashlib.sha256(data).hexdigest(), 2, len(data))}
        self.entries[-1] = ('public.txt', b'100644', b'blob', oid.encode())
        with self.controls() as (fetch, _, _):
            result = self.inspect()
        self.assert_issues(result)
        self.assertEqual(result['text_status'], 'findings')
        self.assertTrue(result['findings'])
        self.assertEqual({row['oid'] for row in result['findings']}, {oid})
        self.assertNotIn(policy.synthetic_token(), json.dumps(result))

    def test_all_alias_mode_kind_missing_replacement_and_other_denials_refuse_prefetch(self):
        first, second, text = self.entries
        p, mode, kind, oid = first
        variants = [[second, text], [(p, mode, kind, b'b' * 40), second, text],
            [('renamed.jks', mode, kind, oid), second, text], [first, first, second, text],
            [(p, b'100755', kind, oid), second, text], [(p, b'120000', kind, oid), second, text],
            [(p, mode, b'commit', oid), second, text]]
        for name, alias_mode, alias_kind in [('extra.txt', b'100644', b'blob'),
                ('extra.png', b'100644', b'blob'), ('extra.so', b'120000', b'blob'),
                ('extra-link', b'160000', b'commit')]:
            variants.append(self.entries + [(name, alias_mode, alias_kind, oid)])
        variants += [self.entries + [('other.jks', mode, kind, b'c' * 40)],
                     self.entries + [('other.p12', mode, kind, b'c' * 40)]]
        for entries in variants:
            with self.subTest(entries=len(entries)), self.controls(entries) as (fetch, _, _), \
                    patch.object(policy, 'prepare', side_effect=AssertionError('no detector')):
                result = self.inspect()
            self.assertEqual(result['status'], 'issue')
            self.assertEqual(result['evaluation_state'], 'not-started')
            self.assertFalse(result['gate_pass'])
            fetch.assert_not_called()

    def test_both_protected_oids_dominate_aliases_in_either_order(self):
        for oid in diagnostic.BINDINGS.values():
            for mode, kind in [(b'100644', b'blob'), (b'120000', b'blob'), (b'160000', b'commit')]:
                for reverse in (False, True):
                    entries = self.entries + [('alias.txt', mode, kind, oid.encode())]
                    if reverse:
                        entries.reverse()
                    with self.controls(entries) as (fetch, _, _):
                        result = self.inspect()
                    self.assert_issues(result)
                    fetch.assert_not_called()

    def test_ancestor_tree_drift_or_ancestry_failure_retains_denials_without_fetch(self):
        for failure in (b'changed', reader.InspectionError('git-inspection-failed')):
            with self.controls() as (fetch, _, _), patch.object(diagnostic, 'git', side_effect=
                    failure if isinstance(failure, Exception) else lambda *a, **k: failure):
                result = self.inspect()
            self.assert_issues(result)
            fetch.assert_not_called()

    def test_staged_provenance_uses_resolved_head_and_exact_ancestor(self):
        with self.controls(commit=None) as (fetch, metadata, _), \
                patch.object(policy, 'prepare', side_effect=OSError('synthetic failure')):
            result = policy.inspect(self.root, staged=True, archive=ARCHIVE, diagnostic_inherited_jks=True)
        self.assert_issues(result)
        self.assertEqual(metadata.call_args_list[0].args[1:], ('rev-parse', '--verify', 'HEAD^{commit}'))
        self.assertEqual(metadata.call_args_list[1].args[1:], ('merge-base', '--is-ancestor', diagnostic.ANCESTOR, self.commit))

    def test_empty_text_cohort_is_not_reported_clean(self):
        with self.controls(self.entries[:2]) as (fetch, _, _):
            result = self.inspect()
        self.assert_issues(result)
        self.assertEqual(result['text_status'], 'not-started')
        fetch.assert_not_called()

    def test_generic_failure_boundaries_retain_exact_denials_and_selection_clear_findings(self):
        boundaries = [(reader, 'fetch_objects'), (policy, 'prepare'), (policy, 'witness'),
                      (payload, 'scan'), (policy, 'map_findings')]
        for module, name in boundaries:
            with self.subTest(boundary=name), self.controls(), \
                    patch.object(module, name, side_effect=OSError('unexported private diagnostic')):
                result = self.inspect()
            self.assert_issues(result)
            self.assertEqual(result['findings'], [])
            self.assertEqual(result['text_status'], 'failed')
            self.assertNotIn('unexported', json.dumps(result))

    def test_pre_and_post_recheck_errors_keep_original_denials(self):
        for index in (0, 1):
            with self.controls() as (_, _, recheck), \
                    patch.object(policy, 'witness'), patch.object(payload, 'scan', return_value=({}, {})):
                recheck.side_effect = [OSError('private')] if index == 0 else [None, OSError('private')]
                result = self.inspect()
            self.assert_issues(result)
            self.assertEqual(result['findings'], [])

    def test_deadline_immediately_after_metadata_retains_denials_before_authorization(self):
        with self.controls():
            selected = reader.read_credentials(self.root, revision=self.commit,
                deadline=time.monotonic() + 5, diagnostic_inherited_jks=True)
            with patch.object(policy.budget, 'tick', side_effect=[None, ScannerError('scanner-deadline')]), \
                    patch.object(reader, 'read_credentials', return_value=selected):
                result = self.inspect()
        self.assert_issues(result)

    def test_cli_explicit_diagnostic_is_always_exit2(self):
        argv = ['credential_content', '--root', str(self.root), '--revision', self.commit,
                '--archive', str(ARCHIVE), '--diagnostic-inherited-jks']
        with self.controls(), patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(policy.main(), 2)
        self.assert_issues(json.loads(output.getvalue()))

    def test_actual_owned_git_ancestor_metadata_and_disjoint_blob_read_spy(self):
        reader.git(self.root, 'init', '-q')
        reader.git(self.root, 'config', 'user.name', 'gandalf196')
        reader.git(self.root, 'config', 'user.email', 'gandalf196@users.noreply.github.com')
        bindings = {}
        for index, path in enumerate(diagnostic.BINDINGS):
            # Inert invented bytes, not a key store or copied binary payload.
            oid = reader.git(self.root, 'hash-object', '-w', '--stdin',
                data=f'public inert manifest fixture {index}\n'.encode()).strip().decode()
            bindings[path] = oid
            reader.git(self.root, 'update-index', '--add', '--cacheinfo', '100644,' + oid + ',' + path)
        (self.root / 'public.txt').write_bytes(b'ordinary public synthetic text\n')
        reader.git(self.root, 'add', 'public.txt')
        reader.git(self.root, 'commit', '-qm', 'Synthetic public metadata fixture')
        ancestor = reader.git(self.root, 'rev-parse', 'HEAD').strip().decode()
        original, reads = reader.read_object, []
        def observe(root, fmt, oid, size, deadline):
            reads.append(oid.decode())
            self.assertNotIn(oid.decode(), bindings.values())
            return original(root, fmt, oid, size, deadline)
        with patch.object(diagnostic, 'BINDINGS', bindings), patch.object(diagnostic, 'ANCESTOR', ancestor), \
                patch.object(reader, 'read_object', side_effect=observe):
            result = policy.inspect(self.root, revision=ancestor, archive=ARCHIVE, diagnostic_inherited_jks=True)
        self.assertEqual(result['status'], 'issue')
        self.assertFalse(result['gate_pass'])
        self.assertEqual(result['text_status'], 'clean')
        self.assertEqual(len(reads), 1)
        self.assertEqual(len(result['issues']), 2)
        self.assertEqual(len(result['uninspected']), 2)


if __name__ == '__main__':
    unittest.main()
