# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact platform/palette metadata bindings, never stored binary payloads."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import credential_content as policy
import credential_scope as scope
import credential_text_gate as gate
import test_credential_jks_diagnostic as fixtures

ADDITIONS = {'osx/dmg_DS_Store': 'f871faa2b9e0d4543fcd0cac871c913bb385be68',
             'config/NEUTRAL.PAL': 'fec2dab03ddf43798aeebbb665c51240390441c1'}


class ExactBinaryBindings(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.JksDiagnostic()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)

    def test_only_exact_metadata_bindings_not_generic_extensions(self):
        self.assertEqual({path:scope.KNOWN_BINARY_BINDINGS[path] for path in ADDITIONS}, ADDITIONS)
        self.assertEqual(len(scope.KNOWN_BINARY_BINDINGS), 4)
        for path in ('other_DS_Store','other.PAL','other.bin','other','other.def'):
            self.assertEqual(scope.classify(path)[0], 'text')
        for suffix in ('pal','def','bin'):
            self.assertNotIn(suffix, scope.EXCLUDED_SUFFIXES)

    def test_exact_added_metadata_counted_without_fetching_payload(self):
        f = self.fixture
        entries = f.entries + [(p,b'100644',b'blob',oid.encode()) for p,oid in ADDITIONS.items()]
        with f.controls(entries) as (fetch, _, _), patch.object(policy, 'witness'), \
                patch.object(policy.payload,'scan',return_value=({},{})):
            result = f.inspect()
        f.assert_issues(result)
        self.assertEqual(result['counts']['known_binary_entries'], 2)
        self.assertEqual(result['counts']['known_binary_objects'], 2)
        self.assertEqual(fetch.call_args.args[2], {f.oid.encode()})

    def test_stale_replaced_duplicate_modes_and_aliases_refuse_before_fetch(self):
        f = self.fixture
        for path, oid in ADDITIONS.items():
            primary = (path,b'100644',b'blob',oid.encode())
            variants = [[(path,b'100644',b'blob',b'e'*40)], [primary,primary]]
            variants += [[(path,mode,kind,oid.encode())] for mode,kind in
                         ((b'100755',b'blob'),(b'120000',b'blob'),(b'160000',b'commit'))]
            variants += [[primary,(alias,mode,kind,oid.encode())] for alias in ('alias.txt','alias.jar','alias.png')
                         for mode,kind in ((b'100644',b'blob'),(b'120000',b'blob'),(b'160000',b'commit'))]
            variants.append([('renamed',b'100644',b'blob',oid.encode())])
            for additions in variants:
                for reverse in (False,True):
                    entries = f.entries + additions
                    if reverse:
                        entries.reverse()
                    with f.controls(entries) as (fetch,_,_), patch.object(policy,'prepare') as detector:
                        result = f.inspect()
                    f.assert_issues(result)
                    self.assertTrue(any(i['code']=='known-binary-binding-mismatch' for i in result['issues']))
                    fetch.assert_not_called()
                    detector.assert_not_called()

    def test_real_synthetic_text_pipeline_satisfies_gate_not_binary_clearance(self):
        f = self.fixture
        with f.controls():
            result = f.inspect()
        f.assert_issues(result)
        self.assertTrue(gate.accepts_supported_text(result))
        self.assertEqual(result['counts']['evaluated_text_aliases'], 1)
        self.assertFalse(result['gate_pass'])
