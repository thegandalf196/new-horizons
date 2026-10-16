# SPDX-License-Identifier: GPL-2.0-or-later
"""Witness failfast caps remain subordinate to the single whole-run deadline."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import credential_content as policy
import credential_payload as payload
import credential_scope as scope
import git_credential_blobs as reader
import scanner_runtime as runtime


class WitnessBudget(unittest.TestCase):
    def fixture(self):
        data = b'ordinary synthetic text\n'
        oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        return {oid: reader.VerifiedObject(oid, data, hashlib.sha256(data).hexdigest(), 2)}, [dict(oid=oid, rules=())]

    def test_fixed_witness_detector10_selected240_other_argv_exact(self):
        selected = runtime.command('/binary', '/template', '/cwd', '/payload')
        witness = runtime.command('/binary', '/template', '/cwd', '/payload', witness=True)
        self.assertEqual(selected.count('--timeout=240'), 1)
        self.assertEqual(witness.count('--timeout=10'), 1)
        self.assertEqual([arg for arg in selected if not arg.startswith('--timeout=')],
                         [arg for arg in witness if not arg.startswith('--timeout=')])

    def test_actual_payload_path_witness12_selected240_and_same_remaining_deadline(self):
        objects, aliases = self.fixture()
        for witness, cap, flag in ((True, 12, '--timeout=10'), (False, 240, '--timeout=240')):
            deadline = time.monotonic() + 2
            parents = []
            def inert(argv, data, cwd, actual_deadline, **kwargs):
                self.assertEqual(actual_deadline, deadline)
                self.assertEqual(kwargs, {'timeout': cap})
                self.assertIn(flag, argv)
                self.assertEqual(data, b'')
                self.assertEqual(list(Path(cwd).iterdir()), [])
                parents.append(Path(cwd).parent)
                total = sum(p.stat().st_size for p in Path(argv[2]).iterdir())
                return 0, b'[]', f'scanned ~{total} bytes\n'.encode()
            with patch.object(payload, 'run', side_effect=inert):
                result, counts = payload.scan('/binary', '/template', objects, aliases, deadline, witness=witness)
            self.assertEqual(result, {})
            self.assertEqual(counts['representations'], 1)
            self.assertTrue(parents and all(not parent.exists() for parent in parents))

    def test_policy_two_file_witness_explicit_mode_keeps_whole_deadline(self):
        deadline = time.monotonic() + 5
        def positive(binary, template, objects, aliases, actual_deadline, **kwargs):
            self.assertEqual(actual_deadline, deadline)
            self.assertEqual(kwargs, {'witness': True})
            self.assertEqual(len(objects), 2)
            self.assertEqual(len(aliases), 2)
            return {(aliases[0]['oid'], ()): [dict(rule_sha256=hashlib.sha256(b'github-pat').hexdigest())]}, {}
        with patch.object(policy, 'run', return_value=(0, b'8.30.1\n', b'')) as version, \
                patch.object(payload, 'scan', side_effect=positive):
            policy.witness('/binary', '/template', '/owned', deadline)
        self.assertEqual(version.call_args.args[3], deadline)

    def test_expired_whole_deadline_refuses_before_version_or_payload(self):
        with patch.object(policy, 'run', side_effect=AssertionError('no child')), \
                patch.object(payload, 'scan', side_effect=AssertionError('no payload')):
            with self.assertRaisesRegex(runtime.ScannerError, '^scanner-deadline$'):
                policy.witness('/binary', '/template', '/owned', 0)

    def assert_reaped(self, children):
        self.assertEqual(len(children), 1)
        child = children[0]
        self.assertIsNotNone(child.returncode)
        self.assertFalse(Path('/proc', str(child.pid)).exists())
        self.assertTrue(all(s.closed for s in (child.stdin, child.stdout, child.stderr)))

    def test_actual_closed_pipe_whole_remaining_beats_witness12_and_cleans_payload(self):
        objects, aliases = self.fixture()
        original_run, original_spawn = runtime.run, subprocess.Popen
        children, parents = [], []
        def spawn(*args, **kwargs):
            child = original_spawn(*args, **kwargs)
            children.append(child)
            return child
        def stuck(argv, data, cwd, deadline, **kwargs):
            self.assertIn('--timeout=10', argv)
            self.assertEqual(kwargs, {'timeout': 12})
            parents.append(Path(cwd).parent)
            code = 'import os,time;os.close(0);os.close(1);os.close(2);time.sleep(30)'
            return original_run([sys.executable, '-c', code], data, cwd, deadline, **kwargs)
        with patch.object(payload, 'run', side_effect=stuck), \
                patch.object(runtime.subprocess, 'Popen', side_effect=spawn):
            start = time.monotonic()
            with self.assertRaisesRegex(runtime.ScannerError, '^scanner-deadline$'):
                payload.scan('/binary', '/template', objects, aliases, start + .2, witness=True)
        self.assertLess(time.monotonic() - start, 3)
        self.assert_reaped(children)
        self.assertTrue(parents and all(not parent.exists() for parent in parents))

    def test_actual_transport12_beats_long_whole_deadline_with_fault_clock_and_cleanup(self):
        original_spawn = subprocess.Popen
        children = []
        def spawn(*args, **kwargs):
            child = original_spawn(*args, **kwargs)
            children.append(child)
            return child
        # Stop=min(whole340,100+12)=112; refuse before any selector wait.
        with tempfile.TemporaryDirectory() as cwd, \
                patch.object(runtime.subprocess, 'Popen', side_effect=spawn), \
                patch.object(runtime.time, 'monotonic', side_effect=[100, 100, 112.01]), \
                patch.object(runtime.selectors.DefaultSelector, 'select', side_effect=AssertionError('must time out before wait')):
            with self.assertRaisesRegex(runtime.ScannerError, '^scanner-deadline$'):
                runtime.run([sys.executable, '-c', 'import time;time.sleep(30)'], b'', cwd, 340, timeout=12)
        self.assert_reaped(children)

    def test_machine_contract_names_whole_selected_and_witness_caps(self):
        limits = scope.contract()['budgets']
        self.assertEqual(limits['whole_seconds'], 240)
        self.assertEqual(limits['selected_detector_seconds'], 240)
        self.assertEqual(limits['selected_transport_seconds'], 240)
        self.assertEqual(limits['witness_detector_seconds'], 10)
        self.assertEqual(limits['witness_transport_seconds'], 12)


if __name__ == '__main__':
    unittest.main()
