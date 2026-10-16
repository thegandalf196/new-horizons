# SPDX-License-Identifier: GPL-2.0-or-later
"""Amended bulk contract: synthetic selection, boundaries and measured finite work."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

import credential_content as policy
import credential_payload as curated
import git_credential_blobs as reader
import git_publication_blobs as finite
import scanner_runtime as runtime
from test_credential_path_aware import object_of

ARCHIVE = Path(os.environ['NH_CREDENTIAL_TEST_ARCHIVE'])


def blob(data):
    return dict(oid=hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest(), data=data)


def objects_and_aliases(blobs):
    objects = {row['oid']: object_of(row['data']) for row in blobs}
    return objects, [dict(oid=row['oid'], rules=()) for row in blobs]


class BatchContract(unittest.TestCase):
    def test_report_malformed_duplicate_fields_lines_and_private_file_rejected(self):
        expected = {'/opaque/file.txt': dict(key=('a' * 40, ()), bytes=4, lines=1)}
        valid = dict(file='/opaque/file.txt', rule='github-pat', start=1, end=1)
        malformed = [b'no', b'{}', b'null', b'[[[]]]', b'[{"rule":"x","rule":"y"}]']
        malformed += [json.dumps([dict(valid, **delta)]).encode() for delta in (
            dict(file='../private'), dict(start=True), dict(start=0), dict(end=2),
            dict(rule='INVALID'), dict(secret='never-export'))]
        for report in malformed:
            with self.assertRaises(runtime.ScannerError):
                curated.parse_result(42, report, b'scanned ~4 bytes\n', expected, time.monotonic()+5)
        for code, report in ((42, b'[]'), (1, b'[]'), (0, json.dumps([valid]).encode())):
            with self.assertRaises(runtime.ScannerError):
                curated.parse_result(code, report, b'scanned ~4 bytes\n', expected, time.monotonic()+5)

    def test_batch_count_and_per_object_count_never_truncate(self):
        expected = {'/opaque/file.txt': dict(key=('a' * 40, ()), bytes=4, lines=1)}
        row = dict(file='/opaque/file.txt', rule='x', start=1, end=1)
        for count in (4097, 16385):
            with self.assertRaisesRegex(runtime.ScannerError, 'scanner-report-count'):
                curated.parse_result(42, json.dumps([row] * count).encode(), b'scanned ~4 bytes\n', expected, time.monotonic()+5)

    def test_deadline_before_materialize_verify_parse_and_no_process(self):
        with tempfile.TemporaryDirectory() as directory:
            for call in (lambda: curated.materialize(Path(directory), {}, 0),
                         lambda: curated.verify(Path(directory), {}, 0),
                         lambda: curated.parse_result(0, b'[]', b'scanned ~0 bytes\n', {}, 0)):
                with self.assertRaisesRegex(runtime.ScannerError, 'scanner-deadline'):
                    call()
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_same_bytes_replacement_inode_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            deadline = time.monotonic()+5
            planned = curated.plan(*objects_and_aliases([blob(b'ordinary\n')]), deadline)
            expected = curated.materialize(path, planned, deadline)
            curated.verify(path, expected, deadline)
            target = next(path.iterdir())
            replacement = path / 'replacement'
            replacement.write_bytes(target.read_bytes())
            replacement.chmod(0o400)
            replacement.replace(target)
            with self.assertRaisesRegex(runtime.ScannerError, 'payload-file-type'):
                curated.verify(path, expected, deadline)


class ActualBulk(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.owned = Path(cls.temporary.name)
        cls.binary, cls.template = runtime.prepare(ARCHIVE, cls.owned)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def scan(self, blobs):
        result, _ = curated.scan(self.binary, self.template, *objects_and_aliases(blobs), time.monotonic()+240)
        return {key[0]: value for key, value in result.items()}

    def test_file_boundaries_are_not_blind_concatenation(self):
        token = policy.synthetic_token()
        halves = [blob(token[:20].encode()), blob(token[20:].encode())]
        positive = blob(('token="' + token + '"\n').encode())
        result = self.scan(halves + [positive])
        self.assertEqual(set(result), {positive['oid']})

    def test_allow_comment_and_hostile_environment_do_not_bypass_bulk(self):
        item = blob(('token="' + policy.synthetic_token() + '" # gitleaks:allow\n').encode())
        with patch.dict(os.environ, {'GITLEAKS_CONFIG': '/not-a-config',
                                   'GITLEAKS_CONFIG_TOML': '[allowlist]\nregexes=[".*"]'}):
            self.assertEqual(set(self.scan([item])), {item['oid']})

    def test_empty_cwd_postcondition_and_payload_cleanup(self):
        original = curated.run
        parents = []
        def tamper(argv, data, cwd, deadline, **kwargs):
            parents.append(Path(cwd).parent)
            result = original(argv, data, cwd, deadline, **kwargs)
            (Path(cwd) / '.gitleaks.toml').write_text('unexpected')
            return result
        with patch.object(curated, 'run', side_effect=tamper):
            with self.assertRaisesRegex(runtime.ScannerError, 'scanner-cwd-not-empty'):
                self.scan([blob(b'ordinary\n')])
        self.assertTrue(parents)
        self.assertTrue(all(not p.exists() for p in parents))

    def test_5111_unique_selected_objects_near_128MiB_whole_pipeline(self):
        """Real reader, hashes, owned files, one batch and final manifest, not startup extrapolation."""
        count, size = 5111, (128 * 1024 * 1024) // 5111
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            finite.git(root, 'init', '-q')
            unit = b'ordinary public source documentation\n'
            for index in range(count):
                prefix = f'public synthetic object {index}\n'.encode()
                if index in (0, count - 1):
                    prefix += ('token="' + policy.synthetic_token() + '"\n').encode()
                body = prefix + (unit * (size // len(unit) + 1))[:size - len(prefix)]
                self.assertEqual(len(body), size)
                suffix = ('py', 'php', 'tf', 'yaml', 'nuget.config')[index % 5]
                (root / f'object-{index:05}.{suffix}').write_bytes(body)
            finite.git(root, 'add', '--all')
            calls, original = [], curated.run
            def track(argv, data, cwd, deadline, **kwargs):
                calls.append(argv)
                self.assertEqual(data, b'')
                self.assertEqual(argv[1], 'dir')
                self.assertIn(len(list(Path(argv[2]).iterdir())), (2, count))
                return original(argv, data, cwd, deadline, **kwargs)
            start = time.monotonic()
            with patch.object(curated, 'run', side_effect=track):
                result = policy.inspect(root, staged=True, archive=ARCHIVE)
            seconds = time.monotonic() - start
            self.assertEqual(result['status'], 'findings')
            self.assertEqual(result['counts']['unique_objects'], count)
            self.assertEqual(result['counts']['evaluated_unique_objects'], count)
            self.assertEqual(result['counts']['representations'], count)
            self.assertEqual(result['counts']['physical_bytes'], count * size)
            self.assertEqual(len(result['counts']['supported_by_applicability']), 5)
            self.assertEqual(len(calls), 2)  # two-file witness + ONE selected batch
            self.assertEqual(len(result['findings']), 2)
            self.assertLess(seconds, 240)
            self.assertNotIn(policy.synthetic_token(), json.dumps(result))
            print(json.dumps({'bulk_benchmark': dict(unique_objects=count, bytes=count * size,
                seconds=round(seconds, 6), selected_scanner_processes=len(calls)-1,
                includes_full_git_reader_and_final_recheck=True)}))


if __name__ == '__main__':
    unittest.main()
