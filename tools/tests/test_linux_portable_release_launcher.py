"""Offline launcher activation fixtures for portable Linux packages."""
# SPDX-License-Identifier: GPL-2.0-or-later

import contextlib
import io
import json
from pathlib import Path
import tempfile
import subprocess
import threading
import unittest
from unittest.mock import patch
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/linux'))

import PortableLauncherLog as launcher_log
import PortableReleaseLauncher as launcher
from PortableLauncherProcess import StartupCancellation, run_process
from PortableReleaseUpdate import PLAYER_EXECUTABLES, PLAYER_FILES


class LinuxPortableReleaseLauncherTest(unittest.TestCase):
    SHA256 = 'c' * 64

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        # The session log of a window started by a test stays in the fixture.
        state = patch.dict('os.environ', {'XDG_STATE_HOME': str(self.root / 'state')})
        state.start()
        self.addCleanup(state.stop)
        self.addCleanup(setattr, launcher_log._session, 'path', None)
        self.addCleanup(setattr, launcher_log._session, 'report_phases', False)
        self.original = self.root / launcher.INSTALL_NAME
        self.store = self.original / launcher.STORE_NAME
        self.request = self.store / 'request.json'
        self.selection = self.store / 'current.json'
        self.updated = self.store / 'releases' / self.SHA256 / launcher.INSTALL_NAME
        self.make_package(self.original, '0.92.3')

    def make_package(self, package, version, exit_code=0):
        for name in PLAYER_FILES:
            path = package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('NHART fixture' if path.suffix == '.nhart' else 'Fixture')
        (package / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': version}))
        for name in PLAYER_EXECUTABLES:
            executable = package / name
            executable.write_text(f'#!/bin/sh\nexit {exit_code}\n')
            executable.chmod(0o700)

    def accept(self, version='0.92.4'):
        self.store.mkdir(mode=0o700, exist_ok=True)
        self.original.joinpath('PortableReleaseUpdate.py').write_text(
            f'print({json.dumps(json.dumps({"version": version, "sha256": self.SHA256}))})\n')
        return {'version': version, 'sha256': self.SHA256}

    def test_successful_probe_selects_update_for_future_launches(self):
        offer = self.accept()
        self.make_package(self.updated, '0.92.4')
        self.assertEqual(launcher.install_release(self.original, offer), self.updated)
        self.assertEqual(launcher.choose_runtime(self.original), self.updated)
        self.assertEqual(launcher.choose_runtime(self.original), self.updated)
        self.assertFalse(self.request.exists())
        self.assertEqual(list(self.store.glob('.accepted-update-*.json')), [])
        self.assertEqual(launcher.version(self.original), (0, 92, 3))

    def test_failed_probe_leaves_current_version_and_cleans_ephemeral_request(self):
        offer = self.accept()
        self.make_package(self.updated, '0.92.4', exit_code=37)
        with self.assertRaises(subprocess.CalledProcessError):
            launcher.install_release(self.original, offer)
        self.assertFalse(self.selection.exists())
        self.assertEqual(list(self.store.glob('.accepted-update-*.json')), [])
        self.assertEqual(launcher.choose_runtime(self.original), self.original)

    def test_missing_runtime_file_prevents_activation_even_if_client_probe_succeeds(self):
        offer = self.accept()
        self.make_package(self.updated, '0.92.4')
        (self.updated / 'config/filesystem.json').unlink()
        with self.assertRaises((ValueError, OSError)):
            launcher.install_release(self.original, offer)
        self.assertFalse(self.selection.exists())

    def test_worker_timeout_gives_the_worker_a_chance_to_clean_up(self):
        marker = self.root / 'cleanup'
        worker = self.root / 'worker.py'
        worker.write_text('import signal\nfrom pathlib import Path\n'
                          'def stop(*args):\n'
                          f'    Path({str(marker)!r}).touch()\n'
                          '    raise SystemExit(1)\n'
                          'signal.signal(signal.SIGTERM, stop)\nsignal.pause()\n')
        with self.assertRaises(InterruptedError):
            launcher.run_worker(worker, self.store, self.request, timeout=1)
        self.assertTrue(marker.exists())

    def test_invalid_selector_cannot_redirect_outside_owned_versions(self):
        self.store.mkdir(mode=0o700)
        self.selection.write_text(json.dumps({'format': 1, 'version': '0.92.4', 'sha256': '../escape'}))
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(launcher.choose_runtime(self.original), self.original)

    def test_linked_store_is_not_used(self):
        other = self.root / 'other'
        other.mkdir(mode=0o700)
        self.store.symlink_to(other, target_is_directory=True)
        self.assertEqual(launcher.choose_runtime(self.original), self.original)
        self.assertEqual(list(other.iterdir()), [])

    def test_next_version_uses_same_profile_and_launch_arguments(self):
        offer = self.accept()
        self.make_package(self.updated, '0.92.4')
        launcher.install_release(self.original, offer)
        with patch.dict('os.environ', {'XDG_DATA_HOME': str(self.root / 'players')}):
            with patch.object(launcher.os, 'execve') as execute:
                launcher.main(['--package', str(self.original), '--', '--launcher-argument', 'value with space'])
        _, arguments, environment = execute.call_args.args
        self.assertEqual(arguments[1], str(self.updated / 'linux_assets_setup.py'))
        self.assertEqual(arguments[-4:], ['--profile', str(self.root / 'players/new-horizons-play'),
                                          '--launcher-argument', 'value with space'])
        self.assertNotIn('NH_PORTABLE_UPDATE_REQUEST', environment)

    def test_verify_only_does_not_create_update_state(self):
        with patch.object(launcher.os, 'execve') as execute:
            launcher.main(['--package', str(self.original), '--', '--verify-only'])
        self.assertFalse(self.store.exists())
        self.assertNotIn('NH_PORTABLE_UPDATE_REQUEST', execute.call_args.args[2])

    def test_no_arguments_opens_desktop_window_without_activating_pending_requests(self):
        self.accept()
        self.request.write_text('legacy request ignored')
        desktop = unittest.mock.Mock()
        desktop.run_window.return_value = 0
        with patch.dict(sys.modules, {'PortableLauncherWindow': desktop}), patch.object(launcher.os, 'execve') as execute:
            self.assertEqual(launcher.main(['--package', str(self.original)]), 0)
        desktop.run_window.assert_called_once_with(self.original, self.original)
        execute.assert_not_called()
        self.assertEqual(self.request.read_text(), 'legacy request ignored')
        self.assertFalse(self.selection.exists())

    def test_profile_setting_disables_automatic_check_without_disabling_manual_checks(self):
        profile = self.root / 'profile'
        settings = profile / 'config/vcmi/settings.json'
        settings.parent.mkdir(parents=True)
        settings.write_text(json.dumps({'general': {'updateChecks': False}, 'other': 'x' * 10000}))
        with patch.object(launcher, 'profile_path', return_value=profile):
            self.assertFalse(launcher.automatic_checks())

    def test_owned_child_drains_stdout_and_stderr_and_handles_readiness_lines(self):
        output, errors = [], []
        code = ('import sys\nprint("NH_LAUNCHER_PREPARING_ART_V1", flush=True)\n'
                'sys.stderr.write("error stream\\n" * 10000)\n'
                'print("NH_LAUNCHER_READY_V1", flush=True)\n')
        result = run_process([sys.executable, '-B', '-c', code], on_stdout=output.append, on_stderr=errors.append)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output, ['NH_LAUNCHER_PREPARING_ART_V1', 'NH_LAUNCHER_READY_V1'])
        self.assertEqual(len(errors), 10000)
        self.assertLess(len(result.stderr), 2000)

    def test_cancelled_child_is_terminated_and_reaped(self):
        cancel = threading.Event()
        code = 'import time\nprint("waiting", flush=True)\ntime.sleep(30)\n'
        with self.assertRaises(InterruptedError):
            run_process([sys.executable, '-B', '-c', code], on_stdout=lambda line: cancel.set(), cancel=cancel)

    def test_startup_cancel_cannot_terminate_a_game_after_readiness(self):
        cancellation = StartupCancellation()
        code = 'import time\nprint("ready", flush=True)\ntime.sleep(0.15)\nprint("finished", flush=True)\n'
        def output(line):
            if line == 'ready':
                cancellation.mark_ready()
                cancellation.set()
        result = run_process([sys.executable, '-B', '-c', code], on_stdout=output, cancel=cancellation)
        self.assertEqual(result.returncode, 0)
        self.assertIn('finished', result.stdout)
        cancellation = StartupCancellation()
        cancellation.set()
        cancellation.mark_ready()
        process = unittest.mock.Mock()
        self.assertFalse(cancellation.terminate_if_set(process))
        process.terminate.assert_not_called()

    def test_closing_launcher_after_readiness_leaves_the_game_drain_active(self):
        import PortableLauncherWindow as desktop
        cancellation = StartupCancellation()
        cancellation.mark_ready()
        state = SimpleNamespace(busy=True, kind='game', ready=cancellation.ready, game_cancel=cancellation,
                                cancel_clicked=unittest.mock.Mock(), set_visible=unittest.mock.Mock())
        self.assertTrue(desktop.LauncherWindow.close_requested(state, None))
        self.assertTrue(state.closed)
        state.set_visible.assert_called_once_with(False)
        state.cancel_clicked.assert_not_called()

    def test_closing_launcher_during_preparation_accepts_startup_cancellation(self):
        import PortableLauncherWindow as desktop
        cancellation = StartupCancellation()
        state = SimpleNamespace(busy=True, closed=False, kind='game', ready=cancellation.ready,
                                game_cancel=cancellation, set_visible=unittest.mock.Mock())
        state.cancel_clicked = lambda button: desktop.LauncherWindow.cancel_clicked(state, button)
        self.assertTrue(desktop.LauncherWindow.close_requested(state, None))
        self.assertTrue(state.closed)
        self.assertTrue(cancellation.requested.is_set())
        state.set_visible.assert_called_once_with(False)

    def test_check_is_bounded_and_returns_only_an_authenticated_unambiguous_offer(self):
        name = 'New-Horizons-Linux-x64-abcdef123456.tar.gz'
        release = {'draft': False, 'prerelease': False, 'tag_name': 'v0.92.4',
                   'html_url': 'https://github.com/thegandalf196/new-horizons/releases/tag/v0.92.4',
                   'published_at': '2026-11-21T12:00:00Z', 'assets': [{
                       'name': name, 'state': 'uploaded', 'size': 1000, 'digest': 'sha256:' + self.SHA256,
                       'browser_download_url': 'https://github.com/thegandalf196/new-horizons/releases/download/v0.92.4/' + name}]}
        with patch.object(launcher, 'latest_release', return_value=release) as latest:
            offer = launcher.check_release(self.original)
        self.assertEqual(latest.call_args.kwargs['timeout'], launcher.REQUEST_TIMEOUT)
        self.assertEqual(offer['sha256'], self.SHA256)
        self.assertEqual(offer['version'], '0.92.4')
        self.assertFalse(self.store.exists())
        release['assets'] *= 2
        with patch.object(launcher, 'latest_release', return_value=release), self.assertRaises(ValueError):
            launcher.check_release(self.original)

    def test_desktop_check_has_overall_owned_process_deadline_and_cancel(self):
        import threading
        cancel = threading.Event()
        result = subprocess.CompletedProcess([], 0, 'null', '')
        with patch.object(launcher, 'run_process', return_value=result) as owned:
            self.assertIsNone(launcher.check_release_bounded(self.original, cancel))
        self.assertEqual(owned.call_args.kwargs['timeout'], launcher.CHECK_DEADLINE)
        self.assertIs(owned.call_args.kwargs['cancel'], cancel)
        self.assertIn('--check-release', owned.call_args.args[0])
        # The deadline names the step still running; the player's own cancel stays a cancel.
        with patch.object(launcher, 'run_process', side_effect=InterruptedError('timed out')):
            with self.assertRaisesRegex(TimeoutError, 'while starting the update check after 45 s'):
                launcher.check_release_bounded(self.original, cancel)
        cancel.set()
        with patch.object(launcher, 'run_process', side_effect=InterruptedError('cancelled')):
            with self.assertRaises(InterruptedError):
                launcher.check_release_bounded(self.original, cancel)

    def test_check_worker_does_not_open_ui_or_select_a_runtime(self):
        with patch.object(launcher, 'check_release', return_value=None), \
             patch.object(launcher, 'choose_runtime') as select, patch('builtins.print') as output:
            self.assertEqual(launcher.main(['--package', str(self.original), '--check-release']), 0)
        select.assert_not_called()
        output.assert_called_once_with('null')

    def test_cancelled_activation_retains_original_selection_and_removes_request(self):
        offer = self.accept()
        self.make_package(self.updated, '0.92.4')
        cancel = threading.Event()
        cancel.set()
        with patch.object(launcher, 'run_worker', return_value=offer), self.assertRaises(InterruptedError):
            launcher.install_release(self.original, offer, cancel=cancel)
        self.assertFalse(self.selection.exists())
        self.assertEqual(list(self.store.glob('.accepted-update-*.json')), [])

    def test_desktop_game_status_waits_for_stdout_marker_and_sets_owned_environment(self):
        import PortableLauncherWindow as desktop
        statuses, environment = [], {}
        state = SimpleNamespace(package=self.original, channel='stable', ready=threading.Event(), cancel=threading.Event(), busy=False,
                                cancel_button=unittest.mock.Mock(),
                                status=unittest.mock.Mock(), status_changed=statuses.append)
        state.begin = lambda kind, operation, finished: finished(operation())
        def process(command, env, on_stdout, cancel):
            environment.update(env)
            self.assertFalse(state.ready.is_set())
            on_stdout('NH_LAUNCHER_PREPARING_ART_V1')
            self.assertFalse(state.ready.is_set())
            on_stdout('unrelated startup text')
            self.assertFalse(state.ready.is_set())
            on_stdout('NH_LAUNCHER_READY_V1')
            self.assertTrue(state.ready.is_set())
            return subprocess.CompletedProcess(command, 0, '', '')
        with patch.object(desktop, 'run_process', side_effect=process):
            desktop.LauncherWindow.play_clicked(state, None)
        self.assertEqual(statuses, ['Preparing artwork and starting the game…', 'Game running.'])
        self.assertEqual(environment['NH_LAUNCHER_STATUS'], '1')
        self.assertEqual(environment['NH_LAUNCHER_MANAGES_UPDATES'], '1')
        self.assertNotIn('NH_PORTABLE_UPDATE_REQUEST', environment)
        state.status.set_text.assert_called_with('Game closed.')

    def release(self, version, prerelease=False, draft=False):
        name = 'New-Horizons-Linux-x64-abcdef123456.tar.gz'
        root = 'https://github.com/thegandalf196/new-horizons/releases/'
        return {'draft': draft, 'prerelease': prerelease, 'tag_name': 'v' + version,
                'html_url': root + 'tag/v' + version, 'published_at': '2026-11-21T12:00:00Z',
                'assets': [{'name': name, 'state': 'uploaded', 'size': 1000, 'digest': 'sha256:' + self.SHA256,
                            'browser_download_url': root + 'download/v' + version + '/' + name}]}

    def test_beta_check_offers_newest_published_release_including_prereleases(self):
        releases = [self.release('0.92.5'), self.release('0.93.1', draft=True),
                    self.release('0.93.0', prerelease=True), {'tag_name': 'nonsense'}, 'not a release']
        with patch.object(launcher, 'recent_releases', return_value=releases) as listed:
            offer = launcher.check_release(self.original, channel='beta')
        self.assertEqual(listed.call_args.kwargs['timeout'], launcher.REQUEST_TIMEOUT)
        self.assertEqual((offer['version'], offer['channel'], offer['format']), ('0.93.0', 'beta', 2))
        with patch.object(launcher, 'latest_release', return_value=self.release('0.93.0', prerelease=True)):
            with self.assertRaises(ValueError):
                launcher.check_release(self.original)
        with patch.object(launcher, 'recent_releases', return_value=[self.release('0.92.3', prerelease=True)]):
            self.assertIsNone(launcher.check_release(self.original, channel='beta'))

    def test_beta_install_selects_only_the_beta_track(self):
        offer = dict(self.accept('0.93.0'), channel='beta')
        self.make_package(self.updated, '0.93.0')
        self.assertIsNone(launcher.choose_runtime(self.original, 'beta'))
        self.assertEqual(launcher.install_release(self.original, offer), self.updated)
        self.assertFalse(self.selection.exists())
        self.assertEqual(launcher.choose_runtime(self.original), self.original)
        self.assertEqual(launcher.choose_runtime(self.original, 'beta'), self.updated)
        self.assertEqual(launcher.track_baseline(self.original, 'beta'), self.updated)
        self.assertEqual(launcher.track_baseline(self.original, 'stable'), self.original)
        with self.assertRaises(ValueError):
            launcher.install_release(self.original, dict(offer, channel='nightly'))

    def test_beta_plays_with_separate_profile_and_reuses_stable_original_files(self):
        players = self.root / 'players'
        def remember(profile, assets):
            setup = players / (profile + '.setup')
            setup.mkdir(mode=0o700, parents=True, exist_ok=True)
            (setup / 'original-assets.json').write_text(json.dumps({'version': 1, 'assets': assets}))
        with patch.dict('os.environ', {'XDG_DATA_HOME': str(players)}):
            self.assertNotIn('--assets', launcher.setup_command(self.original, channel='beta'))
            remember('new-horizons-play', '/games/heroes3')
            stable = launcher.setup_command(self.original)
            beta = launcher.setup_command(self.original, ('--x',), 'beta')
            chooser = launcher.setup_command(self.original, ('--select-assets', '--setup-only'), 'beta')
            remember('new-horizons-play-beta', '/games/other')
            own = launcher.setup_command(self.original, channel='beta')
        self.assertEqual(stable[-2:], ['--profile', str(players / 'new-horizons-play')])
        self.assertEqual(beta[-5:], ['--profile', str(players / 'new-horizons-play-beta'),
                                     '--assets', '/games/heroes3', '--x'])
        self.assertNotIn('--assets', chooser)
        self.assertNotIn('--assets', own)

    def make_beta_package(self, version='0.93.1'):
        self.make_package(self.original, version)
        (self.original / 'BUILD-IDENTITY.json').write_text(json.dumps({'source_commit': 'a' * 40, 'release_channel': 'beta'}))

    def test_package_channel_reads_the_release_marker_and_defaults_to_stable(self):
        self.assertEqual(launcher.package_channel(self.original), 'stable')
        for identity, channel in (({'release_channel': 'beta'}, 'beta'), ({'release_channel': 'stable'}, 'stable'),
                                  ({'release_channel': 'Beta'}, 'stable'), (['release_channel'], 'stable')):
            (self.original / 'BUILD-IDENTITY.json').write_text(json.dumps(identity))
            self.assertEqual(launcher.package_channel(self.original), channel)
        (self.original / 'BUILD-IDENTITY.json').write_text('{"release_channel": "beta"' + ' ' * launcher.MAX_IDENTITY + '}')
        self.assertEqual(launcher.package_channel(self.original), 'stable')

    def test_beta_package_is_the_beta_track_and_starts_with_an_empty_stable_track(self):
        self.make_beta_package()
        self.assertEqual(launcher.choose_runtime(self.original, 'beta'), self.original)
        self.assertIsNone(launcher.choose_runtime(self.original))
        self.assertIsNone(launcher.track_baseline(self.original, 'stable'))
        self.assertEqual(launcher.track_baseline(self.original, 'beta'), self.original)
        # The empty stable track is offered the latest stable even though it is older than the beta.
        with patch.object(launcher, 'latest_release', return_value=self.release('0.92.8')):
            self.assertIsNone(launcher.check_release(self.original))
            offer = launcher.check_release(None)
        self.assertEqual((offer['version'], offer.get('channel')), ('0.92.8', None))
        result = subprocess.CompletedProcess([], 0, json.dumps({'offer': None, 'notes': {}}), '')
        with patch.object(launcher, 'run_process', return_value=result) as owned:
            launcher.check_release_notes_bounded(None, None)
        self.assertIn('--any-version', owned.call_args.args[0])
        with patch.object(launcher, 'check_release', return_value=None) as check, patch('builtins.print'):
            launcher.main(['--package', str(ROOT / 'tools/linux'), '--any-version', '--check-release'])
        self.assertIsNone(check.call_args.args[0])
        # Installing that stable never touches the beta.
        offer = self.accept('0.92.8')
        self.make_package(self.updated, '0.92.8')
        self.assertEqual(launcher.install_release(self.original, offer), self.updated)
        self.assertFalse((self.store / 'beta.json').exists())
        self.assertEqual(launcher.choose_runtime(self.original), self.updated)
        self.assertEqual(launcher.choose_runtime(self.original, 'beta'), self.original)
        self.assertEqual(launcher.track_baseline(self.original, 'stable'), self.updated)
        self.assertEqual(launcher.track_baseline(self.original, 'beta'), self.original)
        # The older stable launcher never takes over the beta package's window.
        (self.updated / 'PortableReleaseLauncher.py').write_text(f'print({launcher.SELF_TEST!r})\n')
        self.assertIsNone(launcher.newer_launcher(self.original))

    def test_beta_package_installs_newer_betas_and_plays_the_beta_profile(self):
        self.make_beta_package()
        offer = dict(self.accept('0.93.1'), channel='beta', format=2)
        self.assertEqual(launcher.install_release(self.original, offer), self.original)
        offer = dict(self.accept('0.93.2'), channel='beta', format=2)
        self.make_package(self.updated, '0.93.2')
        self.assertEqual(launcher.install_release(self.original, offer), self.updated)
        self.assertFalse(self.selection.exists())
        self.assertEqual(launcher.choose_runtime(self.original, 'beta'), self.updated)
        self.assertIsNone(launcher.choose_runtime(self.original))
        code = self.updated / 'PortableReleaseLauncher.py'
        code.write_text(f'print({launcher.SELF_TEST!r})\n')
        self.assertEqual(launcher.newer_launcher(self.original), code)
        players = self.root / 'players'
        desktop = unittest.mock.Mock()
        with patch.dict('os.environ', {'XDG_DATA_HOME': str(players)}), \
                patch.object(launcher, 'newer_launcher', return_value=None), patch.object(launcher.os, 'execve') as execute:
            launcher.main(['--package', str(self.original), '--', '--verify-only'])
            with patch.dict(sys.modules, {'PortableLauncherWindow': desktop}):
                launcher.main(['--package', str(self.original)])
        arguments = execute.call_args.args[1]
        self.assertEqual(arguments[1], str(self.updated / 'linux_assets_setup.py'))
        self.assertEqual(arguments[-3:], ['--profile', str(players / 'new-horizons-play-beta'), '--verify-only'])
        desktop.run_window.assert_called_once_with(self.original, self.updated)

    def test_window_shows_and_checks_an_empty_stable_track(self):
        import PortableLauncherWindow as desktop
        self.make_beta_package()
        label, status = unittest.mock.Mock(), unittest.mock.Mock()
        state = SimpleNamespace(channel='beta', packages={'stable': None, 'beta': self.original}, package=self.original,
                                offer=None, busy=False, version_label=label, status=status, fetched={'stable': None},
                                install=unittest.mock.Mock(), set_busy=unittest.mock.Mock(),
                                check_clicked=unittest.mock.Mock())
        state.refresh_version = lambda: desktop.LauncherWindow.refresh_version(state)
        state.refresh_version()
        label.set_text.assert_called_with('Beta 0.93.1 · separate saves')
        desktop.LauncherWindow.track_changed(state, SimpleNamespace(get_selected=lambda: 0), None)
        self.assertIsNone(state.package)
        label.set_text.assert_called_with('No stable version installed yet. Check for updates to find one.')
        state.check_clicked.assert_called_once_with(None)
        desktop.LauncherWindow.checked(state, 'stable', None, {})
        status.set_text.assert_called_with('No stable release is available right now.')
        desktop.LauncherWindow.checked(state, 'stable', {'version': '0.92.8'}, {})
        status.set_text.assert_called_with('Version 0.92.8 is available.')

    def test_launcher_hands_over_to_the_selected_stable_launcher_after_its_self_test(self):
        self.assertIsNone(launcher.newer_launcher(self.original))
        offer = self.accept()
        self.make_package(self.updated, '0.92.4')
        launcher.install_release(self.original, offer)
        code = self.updated / 'PortableReleaseLauncher.py'
        code.write_text(f'print({launcher.SELF_TEST!r})\n')
        self.assertEqual(launcher.newer_launcher(self.original), code)
        for broken in ('raise SystemExit(3)\n', 'print("something else")\n', 'import time\ntime.sleep(30)\n'):
            code.write_text(broken)
            with patch.object(launcher.subprocess, 'run', wraps=launcher.subprocess.run) as run:
                if 'sleep' in broken:
                    run.side_effect = launcher.subprocess.TimeoutExpired('launcher', 10)
                self.assertIsNone(launcher.newer_launcher(self.original))

    def test_original_launcher_hands_over_once_and_delegated_launcher_runs_its_own_code(self):
        self.addCleanup(setattr, launcher, 'CODE_ROOT', None)
        offer = self.accept()
        self.make_package(self.updated, '0.92.4')
        launcher.install_release(self.original, offer)
        newer = self.updated / 'PortableReleaseLauncher.py'
        with patch.object(launcher, 'newer_launcher', return_value=newer), \
                patch.object(launcher.os, 'execve') as execute:
            launcher.main(['--package', str(self.original), '--', '--verify-only'])
        self.assertEqual(execute.call_args_list[0].args[1][2:],
                         [str(newer), '--package', str(self.original), '--delegated', '--', '--verify-only'])
        with patch.object(launcher, 'newer_launcher') as newer_check, patch.object(launcher.os, 'execve') as execute:
            launcher.main(['--package', str(self.original), '--delegated', '--', '--verify-only'])
        newer_check.assert_not_called()
        self.assertEqual(execute.call_args.args[1][1], str(self.updated / 'linux_assets_setup.py'))
        self.assertEqual(launcher.CODE_ROOT, ROOT / 'tools/linux')

    def test_desktop_completion_releases_application_even_on_bad_finish_callback(self):
        import PortableLauncherWindow as desktop
        application = unittest.mock.Mock()
        state = SimpleNamespace(busy=False, set_busy=unittest.mock.Mock(), status=unittest.mock.Mock(), closed=True,
                                get_application=lambda: application, play_after=False, kind='install', started=0)
        def fail(result):
            raise RuntimeError('startup verification failed')
        self.assertFalse(desktop.LauncherWindow.complete(state, None, None, fail))
        application.release.assert_called_once()
        application.quit.assert_called_once()
        state.status.set_text.assert_called_with('startup verification failed')


if __name__ == '__main__':
    unittest.main()
