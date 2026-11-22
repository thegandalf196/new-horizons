"""Plain-text release notes for the Linux launcher's What's new window."""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/linux'))

import PortableReleaseLauncher as launcher
import PortableReleaseNotes as notes

SHA256 = 'c' * 64
RELEASES = 'https://github.com/thegandalf196/new-horizons/releases/'


def release(version, body=None, prerelease=False, draft=False):
    name = 'New-Horizons-Linux-x64-abcdef123456.tar.gz'
    value = {'draft': draft, 'prerelease': prerelease, 'tag_name': 'v' + version,
             'html_url': RELEASES + 'tag/v' + version, 'published_at': '2026-11-21T12:00:00Z',
             'assets': [{'name': name, 'state': 'uploaded', 'size': 1000, 'digest': 'sha256:' + SHA256,
                         'browser_download_url': RELEASES + 'download/v' + version + '/' + name}]}
    if body is not None:
        value['body'] = body
    return value


class FlattenTest(unittest.TestCase):
    def test_markdown_becomes_plain_text(self):
        text = notes.flatten('## Highlights\r\n\r\n- **Bold** and *italic* and __strong__ and `code`\n'
                             '  * nested ~~old~~ item\n> quoted\n---\n1. numbered\n<details>shown</details>\n'
                             '\\*literal stars\\* &amp; more\n\n\n\nend')
        self.assertEqual(text, 'Highlights\n\n\u2022 Bold and italic and strong and code\n  \u2022 nested old item\n'
                               'quoted\n\n1. numbered\nshown\n*literal stars* & more\n\nend')

    def test_links_lose_their_targets(self):
        text = notes.flatten('See [the wiki](https://example.invalid/wiki "title"), ![map](https://example.invalid/i.png),'
                             ' <https://example.invalid/auto>, https://example.invalid/bare and [ref][1].\n'
                             '[1]: https://example.invalid/reference\nBuilt by [Actions run #7](https://example.invalid/run).')
        self.assertNotIn('example.invalid', text)
        self.assertNotIn('http', text)
        self.assertIn('See the wiki, map, , and ref.', text)
        self.assertIn('Built by Actions run #7.', text)

    def test_control_and_bidirectional_characters_are_removed(self):
        self.assertEqual(notes.flatten('a\x1b[31mb\u202ec\u200bd&#27;e\x00'), 'a[31mbcde')

    def test_size_is_capped_with_a_note(self):
        text = notes.flatten('\n'.join('line %05d' % index for index in range(5000)))
        self.assertLessEqual(len(text), notes.MAX_CHARS + len(notes.SHORTENED))
        self.assertTrue(text.endswith(notes.SHORTENED))
        self.assertTrue(text[:-len(notes.SHORTENED)].endswith(('0', '1', '2', '3', '4', '5', '6', '7', '8', '9')))
        self.assertEqual(len(notes.flatten('x' * 50000)), notes.MAX_CHARS + len(notes.SHORTENED))

    def test_malformed_or_missing_bodies_have_no_notes(self):
        for body in (None, 7, ['list'], {'a': 1}, '', '   \n\n', '<!-- only a comment -->', '---'):
            self.assertIsNone(notes.flatten(body), body)

    def test_hostile_delimiters_stay_fast(self):
        start = time.monotonic()
        for body in ('**a ' * 50000, '# ' + '#' * 100000, '# ' + ' #' * 100000, '[x' * 100000, '![x](' * 40000,
                     '[x][' * 50000, '<a' * 100000, '_a ' * 60000, '__a ' * 50000, '~~a ' * 50000,
                     '<!--' * 50000, '`' * 100000, '`a ' * 60000, '\\*' * 100000, 'a  ' * 70000):
            notes.flatten(body)
        self.assertLess(time.monotonic() - start, 3)


class ComposeTest(unittest.TestCase):
    def test_offer_comes_first_and_both_are_headed(self):
        text = notes.compose('stable', '0.92.7', '0.93.0', {'0.93.0': 'New things'}, 'Installed things')
        self.assertTrue(text.startswith('Stable track'))
        self.assertLess(text.index('Version 0.93.0 (available update)'), text.index('Version 0.92.7 (installed)'))
        self.assertIn('New things', text)
        self.assertIn('Installed things', text)

    def test_fetched_notes_take_precedence_over_bundled_ones(self):
        text = notes.compose('beta', '0.93.1', None, {'0.93.1': 'Published'}, 'Bundled')
        self.assertTrue(text.startswith('Beta track'))
        self.assertIn('Published', text)
        self.assertNotIn('Bundled', text)

    def test_offline_and_missing_messages(self):
        self.assertIn(notes.OFFLINE, notes.compose('stable', '0.92.7'))
        online = notes.compose('stable', '0.92.7', '0.93.0', {}, None)
        self.assertEqual(online.count(notes.MISSING), 2)
        offline = notes.compose('stable', '0.92.7', '0.93.0', None, 'Bundled')
        self.assertIn('Bundled', offline)
        self.assertIn(notes.OFFLINE, offline)
        self.assertIn('No beta is installed yet.', notes.compose('beta', None))
        self.assertIn('Version 0.93.2 (available update)', notes.compose('beta', None, '0.93.2', {'0.93.2': 'x'}))


class BundledTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.package = Path(temporary.name)

    def test_installed_section_of_the_bundled_changelog(self):
        (self.package / notes.BUNDLED_NAME).write_text(
            '# Changelog\n\nIntro [wiki](https://example.invalid).\n\n## 0.92.10 Early Access\n\n- Ten\n\n'
            '## 0.92.1 Early Access\n\n- **One**\n\n## 0.92.0\n\n- Zero\n')
        self.assertEqual(notes.bundled(self.package, '0.92.1'), '\u2022 One')
        self.assertEqual(notes.bundled(self.package, '0.92.10'), '\u2022 Ten')
        self.assertIsNone(notes.bundled(self.package, '0.92.2'))
        self.assertIsNone(notes.bundled(None, '0.92.1'))

    def test_missing_linked_or_oversized_changelog_is_unavailable(self):
        self.assertIsNone(notes.bundled(self.package, '0.92.1'))
        real = self.package / 'elsewhere.md'
        real.write_text('## 0.92.1\n\n- One\n')
        (self.package / notes.BUNDLED_NAME).symlink_to(real)
        self.assertIsNone(notes.bundled(self.package, '0.92.1'))
        (self.package / notes.BUNDLED_NAME).unlink()
        (self.package / notes.BUNDLED_NAME).write_bytes(b'## 0.92.1\n' + b'x' * notes.MAX_BUNDLED_BYTES)
        self.assertIsNone(notes.bundled(self.package, '0.92.1'))

    def test_repository_changelog_has_the_current_version(self):
        current = json.loads((ROOT / 'config/newHorizonsVersion.json').read_text())['version']
        text = (ROOT / 'docs/NEW_HORIZONS_CHANGELOG.md').read_text()
        self.assertTrue(notes.changelog_section(text, current) or current not in text)


class CheckNotesTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.package = Path(temporary.name)
        (self.package / 'config').mkdir()
        (self.package / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': '0.92.7'}))
        # A check-child run in this process reports phases on stderr only for its own test.
        self.addCleanup(setattr, launcher.launcher_log._session, 'report_phases', False)

    def test_stable_check_retains_the_offer_notes_from_the_same_response(self):
        wanted = dict.fromkeys(['0.92.7'])
        with patch.object(launcher, 'latest_release', return_value=release('0.93.0', '- **New**')) as latest:
            offer = launcher.check_release(self.package, notes=wanted)
        latest.assert_called_once()
        self.assertEqual(offer['version'], '0.93.0')
        self.assertEqual(wanted, {'0.92.7': None, '0.93.0': '\u2022 New'})

    def test_stable_check_without_an_update_retains_the_installed_notes(self):
        wanted = dict.fromkeys(['0.92.7'])
        with patch.object(launcher, 'latest_release', return_value=release('0.92.7', 'Installed')):
            self.assertIsNone(launcher.check_release(self.package, notes=wanted))
        self.assertEqual(wanted, {'0.92.7': 'Installed'})

    def test_beta_check_retains_installed_beta_and_offer_but_not_drafts_or_others(self):
        releases = [release('0.93.2', 'Draft', prerelease=True, draft=True), release('0.93.1', 'Offer', prerelease=True),
                    release('0.93.0', 'Installed beta', prerelease=True), release('0.92.9', 'Other'),
                    release('0.92.8', {'not': 'text'}), 'nonsense']
        wanted = dict.fromkeys(['0.93.0', '0.92.8'])
        with patch.object(launcher, 'recent_releases', return_value=releases):
            offer = launcher.check_release(self.package, channel='beta', notes=wanted)
        self.assertEqual(offer['version'], '0.93.1')
        self.assertEqual(wanted, {'0.93.0': 'Installed beta', '0.92.8': None, '0.93.1': 'Offer'})

    def test_check_worker_prints_offer_and_notes_only_when_asked(self):
        def check(original, channel, notes=None):
            notes.update({'0.92.7': 'Text', '0.92.6': None})
            return None
        with patch.object(launcher, 'check_release', side_effect=check), patch('builtins.print') as output:
            self.assertEqual(launcher.main(['--package', str(self.package), '--check-release', '--channel', 'beta',
                                            '--notes-for', '0.92.7']), 0)
        self.assertEqual(json.loads(output.call_args.args[0]), {'offer': None, 'notes': {'0.92.7': 'Text'}})
        with self.assertRaises(ValueError):
            launcher.main(['--package', str(self.package), '--check-release', '--notes-for', '../x'])

    def test_bounded_notes_check_validates_its_child_response(self):
        response = json.dumps({'offer': {'version': '0.93.0'}, 'notes': {'0.93.0': 'Text', 'bad': 'x', '0.92.7': 5}})
        with patch.object(launcher, 'run_process', return_value=subprocess.CompletedProcess([], 0, response, '')) as owned:
            offer, retained = launcher.check_release_notes_bounded(self.package, '0.92.7', threading.Event(), 'beta')
        command = owned.call_args.args[0]
        self.assertEqual(command[command.index('--notes-for') + 1:], ['0.92.7'])
        self.assertEqual(owned.call_args.kwargs['timeout'], launcher.CHECK_DEADLINE)
        self.assertEqual((offer, retained), ({'version': '0.93.0'}, {'0.93.0': 'Text'}))
        with patch.object(launcher, 'run_process', return_value=subprocess.CompletedProcess([], 0, 'null', '')):
            with self.assertRaises(ValueError):
                launcher.check_release_notes_bounded(self.package, None)
        oversized = json.dumps({'offer': None, 'notes': {'0.93.0': 'x' * launcher.MAX_NOTES_RESPONSE}})
        with patch.object(launcher, 'run_process', return_value=subprocess.CompletedProcess([], 0, oversized, '')):
            with self.assertRaises(ValueError):
                launcher.check_release_notes_bounded(self.package, None)


class WindowNotesTest(unittest.TestCase):
    """The window's notes flow without a display: the GTK class is used unbound."""

    def setUp(self):
        try:
            import PortableLauncherWindow as desktop
        except (ImportError, ValueError) as error:
            self.skipTest(f'GTK 4 bindings unavailable: {error}')
        self.desktop = desktop
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.package = Path(temporary.name)
        (self.package / 'config').mkdir()
        (self.package / 'config/newHorizonsVersion.json').write_text(json.dumps({'version': '0.92.7'}))
        (self.package / notes.BUNDLED_NAME).write_text('## 0.92.7 Early Access\n\n- Bundled item\n')

    def state(self, check):
        Window = self.desktop.LauncherWindow
        state = SimpleNamespace(busy=False, channel='stable', offer=None, original=self.package,
                                packages={'stable': self.package, 'beta': None}, cancel=threading.Event(),
                                fetched={'stable': None, 'beta': None}, status=Mock(), install=Mock(), shown=[])
        for name in ('notes_clicked', 'track_check', 'installed_version', 'checked'):
            setattr(state, name, getattr(Window, name).__get__(state))
        state.show_notes = lambda channel: state.shown.append(notes.compose(
            channel, state.installed_version(channel), state.offer and state.offer['version'],
            state.fetched[channel], notes.bundled(state.packages[channel], state.installed_version(channel))))
        state.begin = lambda kind, operation, finished: state.__dict__.update(kind=kind) or finished(operation())
        self.check = patch.object(self.desktop.release, 'check_release_notes_bounded', side_effect=check)
        return state

    def test_first_click_checks_once_then_reuses_the_session_notes(self):
        calls = []
        def check(baseline, installed, cancel, channel):
            calls.append((baseline, installed, channel))
            return {'version': '0.93.0'}, {'0.93.0': 'Offer notes'}
        state = self.state(check)
        with self.check:
            state.notes_clicked(None)
            state.notes_clicked(None)
        self.assertEqual(calls, [(self.package, '0.92.7', 'stable')])
        self.assertEqual(state.kind, 'notes')
        self.assertEqual(state.offer, {'version': '0.93.0'})
        state.install.set_sensitive.assert_called_with(True)
        self.assertEqual(len(state.shown), 2)
        self.assertIn('Offer notes', state.shown[0])
        self.assertIn('Bundled item', state.shown[0])

    def test_offline_click_shows_bundled_notes_and_rechecks_later(self):
        state = self.state(RuntimeError('offline'))
        with self.check:
            state.notes_clicked(None)
        self.assertIsNone(state.fetched['stable'])
        self.assertIn('Bundled item', state.shown[0])
        self.assertIn('Could not reach', state.status.set_text.call_args.args[0])

    def test_busy_launcher_ignores_the_button(self):
        state = self.state(AssertionError('no check while busy'))
        state.busy = True
        with self.check:
            state.notes_clicked(None)
        self.assertEqual(state.shown, [])

    def test_notes_button_follows_the_busy_state(self):
        Window = self.desktop.LauncherWindow
        widgets = {name: Mock() for name in ('check', 'assets', 'notes', 'play', 'track', 'install',
                                             'cancel_button', 'progress')}
        state = SimpleNamespace(package=self.package, offer=None, kind='check', ready=threading.Event(),
                                play_after=False, **widgets)
        Window.set_busy(state, True)
        widgets['notes'].set_sensitive.assert_called_with(False)
        # Play is never blocked by an update check.
        widgets['play'].set_sensitive.assert_called_with(True)
        Window.set_busy(state, False)
        widgets['notes'].set_sensitive.assert_called_with(True)

    def test_first_beta_install_enables_play_and_disables_install(self):
        Window = self.desktop.LauncherWindow
        widgets = {name: Mock() for name in ('check', 'assets', 'notes', 'play', 'track', 'install',
                                             'cancel_button', 'progress', 'status')}
        state = SimpleNamespace(busy=False, channel='beta', package=None, packages={'stable': self.package, 'beta': None},
                                offer={'version': '0.93.0', 'channel': 'beta'}, kind=None, ready=threading.Event(),
                                cancel=threading.Event(), closed=False, original=self.package, play_after=False,
                                started=0, get_application=Mock(), refresh_version=Mock(), update_progress=Mock(),
                                **widgets)
        for name in ('set_busy', 'complete', 'install_clicked'):
            setattr(state, name, getattr(Window, name).__get__(state))
        def begin(kind, operation, finished):
            state.kind = kind
            state.set_busy(True)
            state.complete(operation(), None, finished)
        state.begin = begin
        with patch.object(self.desktop.release, 'install_release', return_value=self.package):
            state.install_clicked(None)
        self.assertIs(state.package, self.package)
        widgets['play'].set_sensitive.assert_called_with(True)
        widgets['install'].set_sensitive.assert_called_with(False)

    def test_notes_window_uses_plain_buffer_text(self):
        source = (ROOT / 'tools/linux/PortableLauncherWindow.py').read_text()
        window = source[source.index('class NotesWindow'):source.index('def run_window')]
        self.assertIn('get_buffer().set_text(text)', window)
        self.assertIn('editable=False', window)
        for markup in ('set_markup', 'insert_markup', 'use_markup', 'show_uri', 'LinkButton', 'launch_default'):
            self.assertNotIn(markup, window)


if __name__ == '__main__':
    unittest.main()
