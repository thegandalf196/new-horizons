# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic tests only: never open a display or emit actual XTest input."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    'nh_private_input', Path(__file__).with_name('nh-private-input.py'))
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class PrivateTextTest(unittest.TestCase):
    def setUp(self):
        self.x = mock.Mock()
        self.x.XStringToKeysym.side_effect = lambda value: value
        self.codes = {}

        def code(display, name):
            return self.codes.setdefault(name, len(self.codes) + 1)

        self.x.XKeysymToKeycode.side_effect = code
        self.t = mock.Mock()
        self.t.XTestFakeKeyEvent.return_value = 1
        self.guard = mock.Mock()

    def test_all_printable_ascii_has_explicit_plan(self):
        self.assertEqual(len(HELPER.ascii_text_plan(''.join(map(chr, range(32, 127))))), 95)
        self.assertEqual(HELPER.ascii_text_plan('aA0):_-'), [
            ('a', False), ('a', True), ('0', False), ('0', True),
            ('semicolon', True), ('minus', True), ('minus', False)])

    def test_scoped_command_colon_uses_shift_semicolon_and_releases(self):
        command = 'vcmiskill new-horizons:learning 1'
        HELPER.send_text(command, self.x, self.t, 191, self.guard)
        self.assertEqual(self.guard.call_count, len(command))
        events = self.t.XTestFakeKeyEvent.call_args_list
        shift = self.codes[b'Shift_L']
        semicolon = self.codes[b'semicolon']
        start = events.index(mock.call(191, shift, 1, 0))
        self.assertEqual(events[start:start + 4], [
            mock.call(191, shift, 1, 0), mock.call(191, semicolon, 1, 0),
            mock.call(191, semicolon, 0, 0), mock.call(191, shift, 0, 0)])
        self.assertEqual(self.x.XFlush.call_count, len(command))

    def test_unsupported_text_refuses_before_any_events_or_lookup(self):
        for value in ('good\n', 'good\t', 'good\0', 'good\x7f', 'goodé', 'x' * 4097):
            with self.subTest(value=value[:8]):
                with self.assertRaises(ValueError):
                    HELPER.send_text(value, self.x, self.t, 191, self.guard)
        self.t.XTestFakeKeyEvent.assert_not_called()
        self.x.XStringToKeysym.assert_not_called()

    def test_unavailable_later_key_preflights_before_events(self):
        self.x.XKeysymToKeycode.side_effect = lambda display, name: 0 if name == b'semicolon' else 1
        with self.assertRaises(ValueError):
            HELPER.send_text('a:', self.x, self.t, 191, self.guard)
        self.t.XTestFakeKeyEvent.assert_not_called()

    def test_failed_press_releases_key_and_shift_finally(self):
        def fail_press(display, key, down, delay):
            if down and key == self.codes[b'semicolon']:
                raise RuntimeError('synthetic event failure')
            return 1

        self.t.XTestFakeKeyEvent.side_effect = fail_press
        with self.assertRaises(RuntimeError):
            HELPER.send_text(':', self.x, self.t, 191, self.guard)
        self.assertEqual(self.t.XTestFakeKeyEvent.call_args_list[-2:], [
            mock.call(191, self.codes[b'semicolon'], 0, 0),
            mock.call(191, self.codes[b'Shift_L'], 0, 0)])
        self.x.XFlush.assert_called_once_with(191)

    def test_shift_release_attempted_even_when_key_release_raises(self):
        def fail_release(display, key, down, delay):
            if not down and key == self.codes[b'semicolon']:
                raise RuntimeError('synthetic release failure')
            return 1

        self.t.XTestFakeKeyEvent.side_effect = fail_release
        with self.assertRaises(RuntimeError):
            HELPER.send_text(':', self.x, self.t, 191, self.guard)
        self.assertEqual(self.t.XTestFakeKeyEvent.call_args_list[-1],
                         mock.call(191, self.codes[b'Shift_L'], 0, 0))
        self.x.XFlush.assert_called_once_with(191)

    def test_guard_failure_blocks_next_character_without_pressed_keys(self):
        self.guard.side_effect = [None, SystemExit('synthetic stale guard')]
        with self.assertRaises(SystemExit):
            HELPER.send_text(':a', self.x, self.t, 191, self.guard)
        self.assertEqual(self.t.XTestFakeKeyEvent.call_count, 4)
        self.assertEqual(self.t.XTestFakeKeyEvent.call_args_list[-1],
                         mock.call(191, self.codes[b'Shift_L'], 0, 0))

    def test_main_refuses_stale_guard_before_loading_x_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            guard = Path(directory) / 'guard.json'
            guard.write_text(json.dumps({'synthetic': True}), encoding='ascii')
            with mock.patch.object(HELPER, 'check_owner', side_effect=SystemExit('stale')), \
                    mock.patch.object(HELPER.c, 'CDLL') as library:
                with self.assertRaises(SystemExit):
                    HELPER.main(['--guard', str(guard), 'text', ':'])
                library.assert_not_called()

    def test_main_prevalidates_later_text_before_earlier_move(self):
        with tempfile.TemporaryDirectory() as directory:
            guard = Path(directory) / 'guard.json'
            guard.write_text('{}', encoding='ascii')
            with mock.patch.object(HELPER.c, 'CDLL') as library:
                with self.assertRaises(ValueError):
                    HELPER.main(['--guard', str(guard), 'move', '1', '2', 'text', 'bad\n'])
                library.assert_not_called()

    def test_main_opens_only_private_display_and_closes_after_text_failure(self):
        self.x.XOpenDisplay.return_value = 191
        self.t.XTestFakeKeyEvent.side_effect = RuntimeError('synthetic failure')
        with tempfile.TemporaryDirectory() as directory:
            guard = Path(directory) / 'guard.json'
            guard.write_text('{}', encoding='ascii')
            with mock.patch.object(HELPER, 'check_owner'), \
                    mock.patch.object(HELPER.c, 'CDLL', side_effect=[self.x, self.t]):
                with self.assertRaises(RuntimeError):
                    HELPER.main(['--guard', str(guard), 'text', ':'])
        self.x.XOpenDisplay.assert_called_once_with(b':191')
        self.x.XCloseDisplay.assert_called_once_with(191)
        self.assertEqual(self.t.XTestFakeKeyEvent.call_count, 3)

    def test_existing_actions_keep_their_argv_shapes(self):
        self.assertEqual(HELPER.parse_actions([
            'move', '1', '2', 'click', '3', '4', 'rightdown', '5', '6',
            'rightup', 'key', 'Escape', 'wait', '1']), [
            ('move', ['1', '2']), ('click', ['3', '4']), ('rightdown', ['5', '6']),
            ('rightup', []), ('key', ['Escape']), ('wait', ['1'])])


if __name__ == '__main__':
    unittest.main()
