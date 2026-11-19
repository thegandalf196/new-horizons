"""Owned-session cancellation receipts; no game, console event, or GUI is executed."""
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POWERSHELL = os.environ.get('NH_TEST_POWERSHELL') or shutil.which('powershell') or shutil.which('pwsh')


@unittest.skipUnless(POWERSHELL, 'Set NH_TEST_POWERSHELL for receipt fixtures')
class WindowsSessionDispositionTest(unittest.TestCase):
    def execute(self, status, existing_status=None, active=True):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = root / ('run-' + 'a' * 32)
            session.mkdir()
            (session / 'pending').write_text('1\n')
            (session / 'client.log').write_text('preserved log')
            if active:
                (session / 'active.lock').touch()
            if existing_status is not None:
                (session / 'launcher-cancelled').write_text('NH_LAUNCH_CANCEL_V1\n')
                (session / 'launcher-exit').write_text('NH_LAUNCH_EXIT_V1\n' + str(existing_status) + '\n')
            other = root / ('run-' + 'b' * 32)
            other.mkdir()
            (other / 'pending').write_text('unrelated')
            harness = root / 'session.ps1'
            harness.write_text(". $env:NH_HELPER\nComplete-NativeClientSession $env:NH_SESSION ([int]$env:NH_STATUS)\n")
            result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-File', str(harness)],
                                    env=dict(os.environ, NH_HELPER=str(ROOT / 'tools/windows/Launch-Process.ps1'),
                                             NH_SESSION=str(session), NH_STATUS=str(status)),
                                    text=True, capture_output=True, timeout=20, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((other / 'pending').read_text(), 'unrelated')
            self.assertEqual((session / 'client.log').read_text(), 'preserved log')
            self.assertEqual(list(session.glob('.launcher-*')), [])
            return {path.name: path.read_text() for path in session.iterdir()}

    def test_control_c_exit_clears_only_the_exact_owned_session(self):
        records = self.execute(-1073741510)
        self.assertNotIn('pending', records)
        self.assertEqual(records['launcher-cancelled'], 'NH_LAUNCH_CANCEL_V1\n')
        self.assertEqual(records['launcher-exit'], 'NH_LAUNCH_EXIT_V1\n-1073741510\n')

    def test_real_fault_and_unknown_exits_remain_pending(self):
        for status in (-1073741819, 3, 17, 0, 130):
            with self.subTest(status=status):
                records = self.execute(status)
                self.assertIn('pending', records)
                self.assertNotIn('launcher-cancelled', records)
                self.assertEqual(records['launcher-exit'], 'NH_LAUNCH_EXIT_V1\n' + str(status) + '\n')

    def test_existing_fault_or_unknown_disposition_overrides_later_cancel(self):
        for status in (139, 17):
            with self.subTest(status=status):
                records = self.execute(-1073741510, existing_status=status)
                self.assertIn('pending', records)
                self.assertEqual(records['launcher-exit'], 'NH_LAUNCH_EXIT_V1\n' + str(status) + '\n')

    def test_no_active_capture_is_not_modified(self):
        records = self.execute(-1073741510, active=False)
        self.assertEqual(set(records), {'pending', 'client.log'})


if __name__ == '__main__':
    unittest.main()
