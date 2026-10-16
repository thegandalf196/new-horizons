# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact nonsecret metadata dispositions preserve every detected record."""
from collections import Counter
import copy
import json
from pathlib import Path
import sys
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import credential_content as content
import credential_disposition as disposition
import credential_text_gate as gate
from scanner_runtime import ScannerError
import test_credential_jks_diagnostic as fixtures
from test_credential_text_gate import clean_text


def raw_reviewed(rows):
    return [{key:value for key,value in row.items() if key != 'disposition'} for row in rows]


class ExactDispositions(unittest.TestCase):
    def split(self, rows):
        original = copy.deepcopy(rows)
        result = disposition.partition(rows, time.monotonic()+5)
        unresolved, reviewed, counts = result
        canonical = lambda items: Counter(json.dumps(row, sort_keys=True) for row in items)
        self.assertEqual(canonical(original), canonical(unresolved + raw_reviewed(reviewed)))
        self.assertEqual(rows, original)
        self.assertEqual(counts, dict(detected=len(rows), reviewed_nonsecret=len(reviewed), unresolved=len(unresolved)))
        return result

    def test_exact_old_and_new_generation_versions_and_runtime_preserve_metadata(self):
        old, new, runtime = disposition.records()
        for generation in (old,new):
            unresolved, reviewed, counts = self.split([generation,runtime])
            self.assertEqual(unresolved, [])
            self.assertEqual(raw_reviewed(reviewed), [generation,runtime])
            self.assertEqual({row['disposition'] for row in reviewed}, {disposition.DISPOSITION})
            self.assertTrue(disposition.valid_partition(unresolved,reviewed,counts))

    def test_wrong_rule_oid_path_or_exact_line_stays_unresolved(self):
        for approved in disposition.records():
            for key,value in [('rule_sha256','e'*64),('oid','e'*40),('path_sha256','e'*64),
                              ('start',approved['start']+1),('end',approved['end']+1),('start',True)]:
                changed = dict(approved, **{key:value})
                unresolved, reviewed, _ = self.split([changed])
                self.assertEqual(unresolved, [changed])
                self.assertEqual(reviewed, [])

    def test_duplicate_known_records_all_stay_unresolved(self):
        for row in disposition.records():
            unresolved, reviewed, _ = self.split([row,row])
            self.assertEqual(unresolved, [row,row])
            self.assertEqual(reviewed, [])

    def test_two_versions_of_same_logical_path_are_not_both_reviewed(self):
        old,new,runtime = disposition.records()
        unresolved,reviewed,_ = self.split([old,new,runtime])
        self.assertEqual(unresolved,[old,new])
        self.assertEqual(raw_reviewed(reviewed),[runtime])

    def test_unknown_extra_record_or_field_is_never_discarded(self):
        known = disposition.records()[0]
        unknown = dict(known, path_sha256='f'*64)
        for row in (unknown, dict(known, unexpected='not-an-approval')):
            unresolved,reviewed,_ = self.split([known,row])
            self.assertEqual(unresolved,[row])
            self.assertEqual(raw_reviewed(reviewed),[known])

    def reviewed_result(self):
        result = clean_text()
        unresolved,reviewed,counts = self.split([disposition.records()[1],disposition.records()[2]])
        result.update(findings=unresolved,reviewed_nonsecret_findings=reviewed,finding_counts=counts)
        return result

    def test_gate_accepts_exact_reviewed_pair_with_original_jks_issues(self):
        result = self.reviewed_result()
        self.assertTrue(gate.accepts_supported_text(result))
        self.assertEqual(len(result['issues']),2)
        self.assertEqual(result['status'],'issue')
        self.assertFalse(result['gate_pass'])

    def test_gate_refuses_duplicate_changed_extra_or_false_reviewed_metadata(self):
        for mode in ('duplicate','changed','extra-field','disposition','missing','two-versions'):
            result = self.reviewed_result()
            rows = result['reviewed_nonsecret_findings']
            if mode == 'duplicate':
                rows[1] = dict(rows[0])
            elif mode == 'changed':
                rows[0]['oid'] = 'e'*40
            elif mode == 'extra-field':
                rows[0]['approved'] = True
            elif mode == 'disposition':
                rows[0]['disposition'] = 'different'
            elif mode == 'missing':
                rows.pop()
            else:
                rows[1] = dict(disposition.records()[0],disposition=disposition.DISPOSITION)
            self.assertFalse(gate.accepts_supported_text(result))

    def test_gate_refuses_dropped_findings_or_inconsistent_accounting(self):
        for mode in ('drop-reviewed','drop-unresolved','change-count','missing-counts','bool-count'):
            result = self.reviewed_result()
            if mode == 'drop-reviewed':
                result['reviewed_nonsecret_findings'] = []
            elif mode == 'drop-unresolved':
                result['finding_counts'].update(detected=3,unresolved=1)
            elif mode == 'change-count':
                result['finding_counts']['detected'] = 1
            elif mode == 'missing-counts':
                result.pop('finding_counts')
            else:
                result['finding_counts']['unresolved'] = False
            self.assertFalse(gate.accepts_supported_text(result))

    def test_gate_refuses_changed_scope_or_remaining_unresolved(self):
        result = self.reviewed_result()
        result['scope']['reviewed_nonsecret_findings']['exact_finding_bindings'].append(dict(oid='e'*40))
        self.assertFalse(gate.accepts_supported_text(result))
        result = self.reviewed_result()
        result['findings'] = [dict(disposition.records()[0],oid='e'*40)]
        result['finding_counts'].update(detected=3,unresolved=1)
        self.assertFalse(gate.accepts_supported_text(result))

    def test_deadline_and_existing_finding_budget_fail_closed(self):
        with self.assertRaisesRegex(ScannerError,'scanner-deadline'):
            disposition.partition(disposition.records(),0)
        with self.assertRaisesRegex(ScannerError,'scanner-total-finding-limit'):
            disposition.partition([disposition.records()[0]]*(disposition.budget.FINDINGS+1),time.monotonic()+5)

    def test_orchestration_clean_only_when_unresolved_empty_and_failure_keeps_denials(self):
        f = fixtures.JksDiagnostic()
        f.setUp()
        self.addCleanup(f.doCleanups)
        rows = [disposition.records()[1],disposition.records()[2]]
        for extra in ([],[dict(rows[0],oid='e'*40)]):
            with f.controls(), patch.object(content,'witness'), patch.object(content.payload,'scan',return_value=({},{})), \
                    patch.object(content,'map_findings',return_value=rows+extra):
                result = f.inspect()
            f.assert_issues(result)
            self.assertEqual(result['text_status'],'findings' if extra else 'clean')
            self.assertEqual(raw_reviewed(result['reviewed_nonsecret_findings']),rows)
            self.assertEqual(result['findings'],extra)
        with f.controls(), patch.object(content,'witness'), patch.object(content.payload,'scan',return_value=({},{})), \
                patch.object(content,'map_findings',return_value=rows), \
                patch.object(disposition,'partition',side_effect=ScannerError('scanner-deadline')):
            result = f.inspect()
        f.assert_issues(result)
        self.assertEqual(result['text_status'],'failed')
        self.assertEqual(result['reviewed_nonsecret_findings'],[])
        self.assertIsNone(result['finding_counts']['detected'])
