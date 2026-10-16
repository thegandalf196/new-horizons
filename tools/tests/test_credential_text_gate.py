# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic structured-transaction gate and pinned archive contracts."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import credential_archive as archive
import credential_text_gate as gate


def clean_text():
    issues = gate.required_issues()
    return dict(schema=3, status='issue', gate_pass=False, evaluation_state='complete',
        text_status='clean', selection_sha256='a'*64, issues=issues, findings=[], reviewed_nonsecret_findings=[],
        finding_counts=dict(detected=0, reviewed_nonsecret=0, unresolved=0),
        scope=gate.scope.contract() | {'diagnostic': gate.legacy.contract()},
        uninspected=[dict(i, disposition=gate.legacy.DISPOSITION, inspected=False) for i in issues],
        counts=dict(total_entries=7, text_aliases=1, evaluated_text_aliases=1,
            evaluated_unique_objects=1, unique_objects=1, representations=1, physical_bytes=20,
            normalized_unique_bytes=20, original_unique_bytes=20, presence_entries=0,
            denied_entries=2, excluded_entries=0, external_gitlinks=0,
            known_binary_entries=4, known_binary_objects=4,
            uninspected_binary_aliases=2, uninspected_binary_objects=2,
            excluded_by_suffix={}, supported_by_applicability={'generic': 1}))


class SupportedTextGate(unittest.TestCase):
    def test_complete_transaction_accepted_without_changing_global_issue(self):
        result = clean_text()
        self.assertTrue(gate.accepts_supported_text(result))
        with patch.object(gate.content, 'inspect', return_value=result) as inspect, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            code = gate.main(['--root', '/synthetic', '--revision', 'b'*40,
                              '--gitleaks-archive', '/synthetic/archive'])
        self.assertEqual(code, 0)
        inspect.assert_called_once_with(Path('/synthetic'), revision='b'*40,
            archive=Path('/synthetic/archive'), diagnostic_inherited_jks=True)
        emitted = json.loads(output.getvalue())
        self.assertEqual(emitted['status'], 'issue')
        self.assertFalse(emitted['gate_pass'])
        self.assertFalse(emitted['supported_text_ci']['binary_safety_established'])

    def test_false_clean_incomplete_failure_and_findings_refuse(self):
        for key, value in [('status','clean'), ('gate_pass',True), ('evaluation_state','incomplete'),
                           ('text_status','failed'), ('text_status','findings'),
                           ('findings',[{'rule_sha256':'b'*64}]), ('selection_sha256','bad'), ('schema',4)]:
            result = clean_text()
            result[key] = value
            self.assertFalse(gate.accepts_supported_text(result))

    def test_extra_missing_duplicate_and_replaced_denials_refuse(self):
        for mode in ('extra','missing','duplicate','oid','path','code'):
            result = clean_text()
            if mode == 'extra':
                result['issues'].append(dict(code='unsupported-relevant-input', oid='e'*40))
            elif mode == 'missing':
                result['issues'].pop()
            elif mode == 'duplicate':
                result['issues'][1] = result['issues'][0]
            else:
                result['issues'][0][{'oid':'oid','path':'path_sha256','code':'code'}[mode]] = 'changed'
            self.assertFalse(gate.accepts_supported_text(result))

    def test_missing_or_changed_provenance_policy_and_bindings_refuse(self):
        for mode in ('ancestor','binding','scope','binary-safety','diagnostic'):
            result = clean_text()
            diagnostic = result['scope']['diagnostic']
            if mode == 'ancestor':
                diagnostic.pop('ancestor')
            elif mode == 'binding':
                diagnostic['binding_path_sha256'] = {}
            elif mode == 'binary-safety':
                diagnostic['binary_safety_established'] = True
            elif mode == 'diagnostic':
                result['scope'].pop('diagnostic')
            else:
                result['scope']['excluded_suffixes'].append('arbitrary')
            self.assertFalse(gate.accepts_supported_text(result))

    def test_uninspected_records_cannot_be_cleared_missing_or_replaced(self):
        for mode in ('missing','inspected','disposition','oid','location'):
            result = clean_text()
            if mode == 'missing':
                result['uninspected'].pop()
            else:
                result['uninspected'][0][mode] = True if mode == 'inspected' else 'changed'
            self.assertFalse(gate.accepts_supported_text(result))

    def test_counts_require_nonzero_complete_consistent_bounded_text(self):
        for key, value in [('evaluated_text_aliases',None), ('evaluated_text_aliases',0),
                           ('evaluated_unique_objects',True), ('unique_objects',2),
                           ('representations',2), ('physical_bytes',0), ('denied_entries',3),
                           ('presence_entries',1), ('total_entries',8), ('known_binary_objects',3),
                           ('original_unique_bytes',gate.budget.UNIQUE_BYTES+1),
                           ('supported_by_applicability',{'pkcs12-file':1}),
                           ('excluded_by_suffix',{'enc':1})]:
            result = clean_text()
            result['counts'][key] = value
            self.assertFalse(gate.accepts_supported_text(result))

    def test_arbitrary_issue_exit_equivalent_is_not_a_passing_cli(self):
        with patch.object(gate.content, 'inspect', return_value=dict(status='issue', findings=[])), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(gate.main(['--root','/synthetic','--revision','b'*40,
                                        '--gitleaks-archive','/synthetic/archive']), 2)
        self.assertFalse(json.loads(output.getvalue())['supported_text_ci']['accepted'])


class ArchiveContract(unittest.TestCase):
    def test_lock_matches_pinned_runtime_and_drift_refuses(self):
        pinned = archive.lock()
        self.assertEqual(pinned['sha256'], archive.ARCHIVE_SHA)
        for changed in (None, {}, pinned | {'url':'https://invalid.example/archive'},
                        pinned | {'version':'unknown'}, pinned | {'bytes':0}, pinned | {'sha256':'bad'}):
            with patch.object(archive.Path, 'read_text', return_value=json.dumps(changed)):
                with self.assertRaises(ValueError):
                    archive.lock()

    def test_unverified_or_linked_archive_never_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'archive'
            target.write_bytes(b'invented corrupt archive')
            alias = Path(directory) / 'alias'
            alias.symlink_to(target)
            for path in (target, alias):
                with self.assertRaises(ValueError):
                    archive.verify(path)
