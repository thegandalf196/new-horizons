# SPDX-License-Identifier: GPL-2.0-or-later
"""Additional inert-only offline/completion/lifecycle regressions; no real repository scans."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import credential_content as policy
import git_credential_blobs as reader
import git_publication_blobs as finite
import credential_payload as payload
import scanner_runtime as runtime

ARCHIVE = Path(os.environ['NH_CREDENTIAL_TEST_ARCHIVE'])
SENTINEL = 'SYNTHETIC-OUTPUT-NEVER-EXPORT'


class Transport(unittest.TestCase):
    def test_io_uring_and_x32_network_entrypoints_also_denied(self):
        code = ('import ctypes,errno\n'
                'libc=ctypes.CDLL(None,use_errno=True)\n'
                'for number in (425,426,427,0x40000000+41):\n'
                ' assert libc.syscall(number,0,0,0,0,0,0)==-1\n'
                ' assert ctypes.get_errno()==errno.EPERM\n'
                'print("OFFLINE-DENIED")\n')
        with tempfile.TemporaryDirectory() as directory:
            status, output, diagnostic = runtime.run([sys.executable, '-c', code], b'', directory,
                time.monotonic() + 5)
        self.assertEqual((status, output, diagnostic), (0, b'OFFLINE-DENIED\n', b''))

    def test_socket_denial_in_child_before_network_contact(self):
        code = ('import socket,errno\n'
                'for family in (socket.AF_INET,socket.AF_INET6,socket.AF_UNIX):\n'
                ' try: socket.socket(family,socket.SOCK_STREAM)\n'
                ' except OSError as e: assert e.errno==errno.EPERM\n'
                ' else: raise AssertionError("network syscall admitted")\n'
                'print("OFFLINE-DENIED")\n')
        with tempfile.TemporaryDirectory() as directory:
            status, output, diagnostic = runtime.run([sys.executable, '-c', code], b'', directory,
                time.monotonic() + 5)
        self.assertEqual((status, output, diagnostic), (0, b'OFFLINE-DENIED\n', b''))

    def test_closed_pipes_still_deadline_kill_and_reap(self):
        children = []
        original = subprocess.Popen
        def spawn(*args, **kwargs):
            child = original(*args, **kwargs)
            children.append(child)
            return child
        code = 'import os,time;os.close(0);os.close(1);os.close(2);time.sleep(5)'
        with tempfile.TemporaryDirectory() as directory, patch.object(runtime.subprocess, 'Popen', side_effect=spawn):
            with self.assertRaisesRegex(runtime.ScannerError, 'scanner-deadline'):
                runtime.run([sys.executable, '-c', code], b'', directory, time.monotonic() + .2)
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].returncode)
        self.assertFalse(Path('/proc', str(children[0].pid)).exists())
        self.assertTrue(all(stream.closed for stream in (children[0].stdin, children[0].stdout, children[0].stderr)))

    def test_resource_abort_and_sandbox_failure_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runtime, 'check_rss', side_effect=runtime.ScannerError('scanner-rss-abort')):
                with self.assertRaisesRegex(runtime.ScannerError, 'scanner-rss-abort'):
                    runtime.run([sys.executable, '-c', 'import time;time.sleep(1)'], b'', directory,
                        time.monotonic() + 5)
            with patch.object(runtime.threading, 'active_count', return_value=2):
                with self.assertRaisesRegex(runtime.ScannerError, 'scanner-network-sandbox'):
                    runtime.run([sys.executable, '-c', 'pass'], b'', directory, time.monotonic() + 5)


class Completion(unittest.TestCase):
    def test_matching_byte_count_with_error_or_skip_never_clean(self):
        for message in (b'ERR failure', b'FTL failure', b'partial scan', b'failed scan', b'skipping', b'issue reading'):
            with self.assertRaisesRegex(runtime.ScannerError, '^scanner-incomplete-diagnostic$'):
                payload.parse_result(0, b'[]', b'scanned ~4 bytes\n' + message + b'\n',
                    {'/opaque': dict(key=('a'*40, ()), bytes=4, lines=1)}, time.monotonic()+5)

    def test_immutable_commit_manifest_rechecked_not_moving_ref(self):
        with patch.object(reader, 'git', return_value=b'fixed') as call:
            reader.recheck('/synthetic', False, b'fixed', time.monotonic()+5, 'a'*40)
            self.assertEqual(call.call_args.args[1:], ('ls-tree', '-rz', '--full-tree', 'a' * 40))
        with patch.object(reader, 'git', return_value=b'changed'):
            with self.assertRaisesRegex(runtime.ScannerError, 'commit-changed-during-scanner'):
                reader.recheck('/synthetic', False, b'fixed', time.monotonic()+5, 'a'*40)

    def test_nonempty_scanner_cwd_refused_before_process(self):
        original = payload.materialize
        def contaminate(directory, planned, deadline):
            (directory.parent/'cwd'/'.gitleaks.toml').write_text('[allowlist]\nregexes=[".*"]\n')
            return original(directory, planned, deadline)
        with patch.object(payload, 'materialize', side_effect=contaminate):
            with patch.object(payload, 'run', side_effect=AssertionError('no process')):
                with self.assertRaisesRegex(runtime.ScannerError, 'scanner-cwd-not-empty'):
                    payload.scan('/unused', '/unused', {}, [], time.monotonic()+5)

    def test_archive_corruption_and_license_member_fail_before_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            wrong = Path(directory) / 'bad'
            wrong.write_bytes(b'corrupt archive')
            with patch.object(runtime, 'ARCHIVE_SIZE', wrong.stat().st_size), \
                    patch.object(runtime, 'ARCHIVE_SHA', hashlib.sha256(wrong.read_bytes()).hexdigest()):
                with self.assertRaisesRegex(runtime.ScannerError, 'scanner-archive-unavailable'):
                    runtime.prepare(wrong, directory)
            original = runtime.tarfile.TarFile.extractfile
            def wrong_notice(tar, member):
                return io.BytesIO(b'x' * 1069) if member == 'LICENSE' else original(tar, member)
            with patch.object(runtime.tarfile.TarFile, 'extractfile', autospec=True, side_effect=wrong_notice):
                with self.assertRaisesRegex(runtime.ScannerError, 'scanner-license-identity'):
                    runtime.prepare(ARCHIVE, directory)


class RealBoundaries(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        parent = Path(cls.temporary.name)
        tools = parent / 'tools'
        tools.mkdir(mode=0o700)
        cls.cwd = parent / 'cwd'
        cls.cwd.mkdir(mode=0o700)
        start = time.monotonic()
        cls.binary, cls.template = runtime.prepare(ARCHIVE, tools)
        cls.archive_prepare_seconds = time.monotonic() - start
        cls.measurements = []

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()
        print(json.dumps({'per_blob_seconds': cls.measurements}, sort_keys=True))

    def scan(self, text):
        start = time.monotonic()
        data = text.encode()
        oid = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        obj = reader.VerifiedObject(oid, data, hashlib.sha256(data).hexdigest(), data.count(b'\n')+1)
        findings, _ = payload.scan(self.binary, self.template, {oid: obj}, [dict(oid=oid,rules=())], start+12)
        result = findings.get((oid, ()), [])
        self.measurements.append(round(time.monotonic() - start, 6))
        return result

    def test_crossing_64kb_100kb_and_100kib_positions(self):
        for boundary in (65536, 100000, 102400):
            body = '#' * (boundary - 18) + ' token="' + policy.synthetic_token() + '"\n'
            self.assertTrue(self.scan(body))

    def test_multiline_synthetic_key_around_chunk_edge(self):
        body = '#' * 99980 + '\n-----BEGIN ' + 'RSA PRIVATE KEY-----\n'
        body += ('QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo=\n' * 3) + '-----END RSA PRIVATE KEY-----\n'
        self.assertTrue(self.scan(body))

    def test_maximum_selected_plaintext_complete_not_skipped(self):
        unit = 'ordinary public documentation\n'
        text = (unit * (finite.MAX_TEXT_BYTES // len(unit) + 1))[:finite.MAX_TEXT_BYTES]
        self.assertEqual(self.scan(text), [])


class RealSelection(unittest.TestCase):
    def test_argv_output_cleanup_and_cli_use_only_selected_index(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for args in [('init', '-q'), ('config', 'user.name', 'gandalf196'),
                         ('config', 'user.email', 'gandalf196@users.noreply.github.com')]:
                finite.git(root, *args)
            token = policy.synthetic_token()
            path = root / (SENTINEL + '.py')
            path.write_text('token="' + token + '"\n')
            finite.git(root, 'add', path.name)
            path.write_text('clean\n')
            original, argv = runtime.subprocess.Popen, []
            def spawn(args, **kwargs):
                argv.append(args)
                return original(args, **kwargs)
            with patch.object(runtime.subprocess, 'Popen', side_effect=spawn):
                result = policy.inspect(root, staged=True, archive=ARCHIVE)
            self.assertEqual(result['status'], 'findings')
            exported = json.dumps(result)
            self.assertNotIn(token, exported)
            self.assertNotIn(SENTINEL, exported)
            self.assertTrue(all(token not in str(args) and SENTINEL not in str(args) for args in argv))
            cli = subprocess.run([sys.executable, '-B', policy.__file__, '--root', str(root), '--staged',
                '--gitleaks-archive', str(ARCHIVE)], capture_output=True, timeout=20)
            self.assertEqual(cli.returncode, 1)
            self.assertEqual(json.loads(cli.stdout)['status'], 'findings')
            self.assertNotIn(token.encode(), cli.stdout + cli.stderr)
            self.assertNotIn(SENTINEL.encode(), cli.stdout + cli.stderr)


if __name__ == '__main__':
    unittest.main()
