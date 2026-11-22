"""Linux launcher session log, update-check deadline and Play during a check."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import re
import socket
import stat
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from urllib.error import URLError

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/linux'))

import PortableLauncherLog as launcher_log
import PortableReleaseLauncher as launcher
from PortableLauncherProcess import run_process
import PortableReleaseUpdate as update

LINE = re.compile(r'\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} [+-]\d{4} \+\d+\.\d{3}s (\S+) +(.*)')


class LinuxLauncherLogTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.home = self.root / 'home/player'
        self.home.mkdir(parents=True)
        environment = patch.dict('os.environ', {'HOME': str(self.home), 'USER': 'player'})
        environment.start()
        self.addCleanup(environment.stop)
        os.environ.pop(launcher_log.ENVIRONMENT, None)
        os.environ.pop('XDG_STATE_HOME', None)
        self.addCleanup(setattr, launcher_log._session, 'path', None)
        self.addCleanup(setattr, launcher_log._session, 'report_phases', False)
        self.log = self.home / '.local/state/new-horizons/launcher.log'

    def lines(self, path=None):
        return [LINE.fullmatch(line).groups() for line in (path or self.log).read_text().splitlines()]

    def test_session_is_private_rotated_redacted_and_continued_after_a_hand_over(self):
        self.assertEqual(launcher_log.start_session(), self.log)
        launcher_log.write_header('0.93.1', self.home / 'Games/New-Horizons-Linux-x64')
        launcher_log.write(f'Profile {self.home}/.local/share and /media/player/disk\nsecond line')
        self.assertEqual(stat.S_IMODE(self.log.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.log.parent.stat().st_mode), 0o700)
        first = self.lines()
        self.assertEqual(first[0], ('launcher', f'New Horizons launcher 0.93.1 (Linux), code '
                                                f'~/Games/New-Horizons-Linux-x64, pid {os.getpid()}'))
        self.assertTrue(first[1][1].startswith('OS: ') and first[2][1].startswith('Python '))
        self.assertEqual(first[3][1], 'Profile ~/.local/share and /media/$USER/disk | second line')
        self.assertNotIn(str(self.home), self.log.read_text())
        before = self.log.read_text()
        with patch.dict('os.environ', launcher_log.child_environment()):
            launcher_log.start_session()
        self.assertTrue(self.log.read_text().startswith(before))
        self.assertIn('Continuing in the launcher', self.log.read_text())
        launcher_log.start_session()
        self.assertTrue((self.log.parent / 'launcher.previous.log').read_text().startswith(before))
        self.assertFalse(self.log.exists())  # The new session starts with its first line.
        self.assertEqual(launcher_log.hint(), ' Details: ~/.local/state/new-horizons/launcher.log')
        with patch.dict('os.environ', {'XDG_STATE_HOME': str(self.root / 'state')}):
            self.assertEqual(launcher_log.log_directory(), self.root / 'state/new-horizons')
        with patch.dict('os.environ', {'XDG_STATE_HOME': 'relative'}):
            self.assertEqual(launcher_log.log_directory(), self.home / '.local/state/new-horizons')

    def test_log_is_bounded_with_one_marker(self):
        launcher_log.start_session()
        with patch.object(launcher_log, 'MAX_BYTES', 2048):
            for index in range(100):
                launcher_log.write(f'line {index} ' + 'x' * 40)
        self.assertLess(self.log.stat().st_size, 2300)
        self.assertEqual(self.log.read_text().count('Log size limit reached'), 1)

    def test_child_appends_to_the_session_and_reports_phases(self):
        launcher_log.start_session()
        code = ('import sys\nsys.path.insert(0, sys.argv[1])\nimport PortableLauncherLog as log\n'
                'log.join_session("check", report_phases=True)\nlog.phase("request")\n')
        result = subprocess.run([sys.executable, '-B', '-c', code, str(ROOT / 'tools/linux')], text=True,
                                capture_output=True, env=launcher_log.child_environment(), check=True, timeout=30)
        self.assertEqual(json.loads(result.stderr), {'phase': 'request'})
        self.assertEqual(self.lines()[-1], ('check', 'Phase: request'))
        game = launcher_log.child_environment({'OTHER': '1', launcher_log.ENVIRONMENT: '/elsewhere/launcher.log'})
        self.assertEqual(game[launcher_log.ENVIRONMENT], str(self.log))

    def test_check_deadline_is_generous_and_requests_have_their_own_bound(self):
        self.assertEqual((launcher.CHECK_DEADLINE, launcher.REQUEST_TIMEOUT), (45, 20))
        self.assertLess(launcher.REQUEST_TIMEOUT, launcher.CHECK_DEADLINE)

    def test_timeout_names_the_phase_the_child_was_in(self):
        launcher_log.start_session()
        code = ('import json, sys, time\nprint(json.dumps({"phase": "request"}), file=sys.stderr, flush=True)\n'
                'time.sleep(30)\n')
        def stalled(command, *arguments, **options):
            return run_process([sys.executable, '-B', '-c', code], *arguments, **options)
        with patch.object(launcher, 'run_process', side_effect=stalled):
            with self.assertRaises(TimeoutError) as caught:
                launcher.bounded_check(self.root, None, 'stable', (), 16384, deadline=1)
        self.assertEqual(str(caught.exception), 'Update check timed out while contacting GitHub after 1 s. '
                                                'You can still play the installed game.')
        self.assertIn('the child was in phase request and was stopped', self.log.read_text())

    def test_failed_request_logs_host_path_and_reason_without_the_query(self):
        launcher_log.start_session()
        opener = Mock(side_effect=URLError(socket.timeout('timed out')))
        with self.assertRaises(URLError):
            update.release_metadata(update.LIST_URL, opener, timeout=launcher.REQUEST_TIMEOUT)
        text = self.log.read_text()
        self.assertIn('HTTP GET api.github.com/repos/thegandalf196/new-horizons/releases (timeout 20 s, proxy ', text)
        self.assertIn('HTTP request to api.github.com/repos/thegandalf196/new-horizons/releases failed after', text)
        self.assertIn('[reason TimeoutError: timed out]', text)
        self.assertNotIn('per_page', text)

    def test_play_during_a_check_stops_it_then_starts_the_game(self):
        import PortableLauncherWindow as desktop
        Window = desktop.LauncherWindow
        widgets = {name: Mock() for name in ('check', 'assets', 'notes', 'play', 'track', 'install',
                                             'cancel_button', 'progress', 'status')}
        state = SimpleNamespace(busy=False, kind='', package=self.root, channel='stable', offer=None, closed=False,
                                play_after=False, started=0, cancel=threading.Event(), ready=threading.Event(),
                                get_application=Mock(), **widgets)
        for name in ('set_busy', 'complete'):
            setattr(state, name, getattr(Window, name).__get__(state))
        state.kind = 'check'
        state.set_busy(True)
        widgets['play'].set_sensitive.assert_called_with(True)
        Window.play_clicked(state, None)
        self.assertTrue(state.play_after and state.cancel.is_set())
        widgets['play'].set_sensitive.assert_called_with(False)
        state.play_clicked = Mock()
        finished = Mock()
        Window.complete(state, None, InterruptedError('Operation cancelled or timed out'), finished)
        finished.assert_not_called()
        state.play_clicked.assert_called_once_with(None)
        self.assertFalse(state.play_after)
        self.assertNotIn('cancelled', str(widgets['status'].set_text.call_args))
        state.kind = 'install'
        state.set_busy(True)
        widgets['play'].set_sensitive.assert_called_with(False)


if __name__ == '__main__':
    unittest.main()
