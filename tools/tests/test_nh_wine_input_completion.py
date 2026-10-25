# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure post-gesture exit races; no processes, Wine, Xvfb or input events."""
import copy
import importlib.util
from pathlib import Path
import signal
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    'wine_input_owner', Path(__file__).with_name('nh_wine_fullgame_owner.py'))
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


class InputCompletionTests(unittest.TestCase):
    def setUp(self):
        self.identity = {'pid': 42, 'start_ticks': 100, 'uid': 1000,
                         'boot_id': 'original-boot', 'state': 'S', 'ppid': 1}
        self.backend = mock.Mock(spec=owner.Linux)
        self.backend.reap.return_value = []
        self.backend.exited.return_value = False
        self.backend.kernel_snapshot.return_value = copy.deepcopy(self.identity)
        self.now = 0
        self.on_sleep = None
        self.runtime = owner.Owner(self.backend, {}, mock.Mock(), clock=lambda: self.now)
        self.runtime.started = 0
        self.runtime.armed.set()
        self.runtime.adopt = mock.Mock()  # Ancestry admission has separate owner tests.
        self.runtime.ledger[42] = {'identity': copy.deepcopy(self.identity), 'pidfd': 142}
        self.factory = mock.Mock(side_effect=lambda: {})
        self.verifier = mock.Mock(return_value={'verified': True})

    def dispatch(self, event):
        def settle_sleep(seconds):
            self.now += seconds
            if self.on_sleep is not None:
                self.on_sleep()
        with mock.patch.object(owner.time, 'sleep', side_effect=settle_sleep):
            return self.runtime.perform_input(self.factory, self.verifier, event, self.identity)

    def exit_zero(self):
        self.backend.reap.return_value = [(42, 0)]
        self.backend.exited.return_value = True

    def denied(self, name='environ'):
        return PermissionError(13, 'private proc read denied', '/proc/42/' + name)

    def test_live_gesture_requires_two_fresh_proofs(self):
        event = mock.Mock()
        self.assertIs(self.dispatch(event), True)
        self.assertEqual(self.factory.call_count, 2)
        self.assertEqual(self.verifier.call_count, 2)
        event.assert_called_once_with()

    def test_reaped_zero_game_stops_input_without_post_proof_or_ack(self):
        self.assertIs(self.dispatch(self.exit_zero), False)
        self.assertEqual(self.factory.call_count, 1)
        self.assertEqual(self.verifier.call_count, 1)
        self.assertEqual(self.runtime.reaped[42], 0)
        self.backend.exited.assert_called_with(142)

    def test_game_exits_during_post_environment_read(self):
        def factory():
            if self.factory.call_count == 2:
                self.exit_zero()
                raise self.denied()
            return {}
        self.factory.side_effect = factory
        self.assertIs(self.dispatch(mock.Mock()), False)
        self.assertEqual(self.factory.call_count, 2)
        self.assertEqual(self.verifier.call_count, 1)

    def test_exit_becomes_reapable_after_post_environment_read_failed(self):
        self.factory.side_effect = [{}, self.denied()]
        self.on_sleep = self.exit_zero
        self.assertIs(self.dispatch(mock.Mock()), False)
        self.assertEqual(self.factory.call_count, 2)
        self.assertEqual(self.verifier.call_count, 1)
        self.assertEqual(self.runtime.reaped[42], 0)
        self.assertAlmostEqual(self.now, .01)

    def test_ready_pidfd_still_waits_for_actual_zero_wait_status(self):
        self.factory.side_effect = [{}, self.denied()]
        self.backend.exited.return_value = True
        self.on_sleep = self.exit_zero
        self.assertIs(self.dispatch(mock.Mock()), False)
        self.assertEqual(self.runtime.reaped[42], 0)
        self.assertAlmostEqual(self.now, .01)

    def test_actual_zero_wait_still_waits_for_retained_pidfd_readiness(self):
        self.factory.side_effect = [{}, self.denied()]
        self.on_sleep = lambda: setattr(self.backend.exited, 'return_value', True)
        event = lambda: setattr(self.backend.reap, 'return_value', [(42, 0)])
        self.assertIs(self.dispatch(event), False)
        self.assertEqual(self.runtime.reaped[42], 0)
        self.assertAlmostEqual(self.now, .01)

    def test_pending_nonzero_exit_refuses_without_full_settle_delay(self):
        error = self.denied()
        self.factory.side_effect = [{}, error]
        def failed_exit():
            self.backend.reap.return_value = [(42, 9)]
            self.backend.exited.return_value = True
        self.on_sleep = failed_exit
        with self.assertRaises(PermissionError) as caught:
            self.dispatch(mock.Mock())
        self.assertIs(caught.exception, error)
        self.assertEqual(self.runtime.reaped[42], 9)
        self.assertAlmostEqual(self.now, .01)

    def test_settle_never_accepts_zero_status_arriving_at_input_deadline(self):
        self.now = owner.INPUT - .005
        self.factory.side_effect = [{}, self.denied()]
        self.on_sleep = self.exit_zero
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertAlmostEqual(self.now, owner.INPUT)

    def test_overslept_settle_cannot_accept_later_zero_exit(self):
        error = self.denied()
        self.factory.side_effect = [{}, error]
        def overslept():
            self.now = owner.POST_INPUT_EXIT_SETTLE + 1
            self.exit_zero()
        self.on_sleep = overslept
        with self.assertRaises(PermissionError) as caught:
            self.dispatch(mock.Mock())
        self.assertIs(caught.exception, error)
        self.assertNotIn(42, self.runtime.reaped)

    def test_reap_crossing_settle_deadline_cannot_accept_zero_status(self):
        self.factory.side_effect = [{}, self.denied()]
        def late_reap():
            if self.backend.reap.call_count == 2:
                self.now = owner.POST_INPUT_EXIT_SETTLE + 1
                return [(42, 0)]
            return []
        self.backend.reap.side_effect = late_reap
        self.backend.exited.return_value = True
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertEqual(self.runtime.reaped[42], 0)

    def test_pidfd_proof_crossing_settle_deadline_cannot_accept(self):
        def factory():
            if self.factory.call_count == 2:
                self.exit_zero()
                raise self.denied()
            return {}
        def late_pidfd(_):
            if 42 in self.runtime.reaped:
                self.now = owner.POST_INPUT_EXIT_SETTLE + 1
                return True
            return False
        self.factory.side_effect = factory
        self.backend.exited.side_effect = late_pidfd
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertEqual(self.runtime.reaped[42], 0)

    def test_nonsettling_pidfd_proof_still_obeys_fixed_input_deadline(self):
        def late_pidfd(_):
            self.now = owner.INPUT
            return True
        self.backend.exited.side_effect = late_pidfd
        with self.assertRaises(owner.Refusal):
            self.dispatch(self.exit_zero)
        self.assertEqual(self.factory.call_count, 1)

    def test_owner_finished_during_settle_remains_refused(self):
        self.factory.side_effect = [{}, self.denied()]
        self.on_sleep = self.runtime.finished.set
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertAlmostEqual(self.now, .01)

    def test_identity_reuse_during_settle_cannot_use_later_zero_status(self):
        self.factory.side_effect = [{}, self.denied()]
        def reused():
            self.runtime.ledger[42]['identity']['start_ticks'] += 1
            self.exit_zero()
        self.on_sleep = reused
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertNotIn(42, self.runtime.reaped)
        self.assertAlmostEqual(self.now, .01)

    def test_graph_error_during_settle_cannot_use_zero_exit(self):
        self.factory.side_effect = [{}, self.denied()]
        def failed_graph():
            self.runtime.errors.append('Unrecorded adopted child reaped')
            self.exit_zero()
        self.on_sleep = failed_graph
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertAlmostEqual(self.now, .01)

    def test_other_process_environment_loss_never_gets_settle(self):
        error = PermissionError(13, 'other process denied', '/proc/43/environ')
        self.factory.side_effect = [{}, error]
        self.on_sleep = self.exit_zero
        with self.assertRaises(PermissionError) as caught:
            self.dispatch(mock.Mock())
        self.assertIs(caught.exception, error)
        self.assertEqual(self.now, 0)

    def test_selected_proc_missing_after_dispatch_needs_actual_zero_exit(self):
        for name in ('stat', 'exe', 'cmdline', 'maps', 'environ'):
            with self.subTest(name=name):
                self.setUp()
                def factory():
                    if self.factory.call_count == 2:
                        self.exit_zero()
                        raise FileNotFoundError(2, 'selected proc removed', '/proc/42/' + name)
                    return {}
                self.factory.side_effect = factory
                self.assertIs(self.dispatch(mock.Mock()), False)

    def test_unreadable_pre_proof_never_dispatches(self):
        self.factory.side_effect = self.denied()
        event = mock.Mock()
        with self.assertRaises(PermissionError):
            self.dispatch(event)
        event.assert_not_called()
        self.backend.reap.assert_not_called()

    def test_live_post_permission_denied_still_fails(self):
        error = self.denied()
        self.factory.side_effect = [{}, error]
        with self.assertRaises(PermissionError) as caught:
            self.dispatch(mock.Mock())
        self.assertIs(caught.exception, error)
        self.assertNotIn(42, self.runtime.reaped)
        self.assertAlmostEqual(self.now, owner.POST_INPUT_EXIT_SETTLE)

    def test_nonzero_exit_does_not_convert_missing_post_proof_to_success(self):
        self.factory.side_effect = [{}, self.denied()]
        def failed_exit():
            self.backend.reap.return_value = [(42, 9)]
            self.backend.exited.return_value = True
        with self.assertRaises(PermissionError):
            self.dispatch(failed_exit)
        self.assertEqual(self.runtime.reaped[42], 9)

    def test_pidfd_readiness_without_wait_status_is_not_zero_exit(self):
        self.backend.exited.return_value = True
        self.factory.side_effect = [{}, self.denied()]
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertNotIn(42, self.runtime.reaped)

    def test_zero_wait_status_without_retained_pidfd_exit_is_not_terminal(self):
        self.factory.side_effect = [{}, self.denied()]
        with self.assertRaises(PermissionError):
            self.dispatch(lambda: setattr(self.backend.reap, 'return_value', [(42, 0)]))
        self.assertEqual(self.runtime.reaped[42], 0)

    def test_parent_reaped_without_actual_wait_status_is_not_zero_exit(self):
        self.backend.exited.return_value = True
        self.runtime.parent_reaped[42] = {'proof': 'retained-pidfd-exited-and-absent'}
        self.factory.side_effect = [{}, self.denied()]
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())
        self.assertNotIn(42, self.runtime.reaped)

    def test_reused_ledger_lifetime_does_not_accept_old_zero_status(self):
        self.factory.side_effect = [{}, self.denied()]
        def replaced():
            self.exit_zero()
            self.runtime.ledger[42]['identity']['start_ticks'] += 1
        with self.assertRaises(PermissionError):
            self.dispatch(replaced)

    def test_changed_pid_uid_or_boot_cannot_use_zero_status(self):
        for field, value in (('pid', 43), ('uid', 2000), ('boot_id', 'other-boot')):
            with self.subTest(field=field):
                self.setUp()
                self.factory.side_effect = [{}, self.denied()]
                def changed():
                    self.exit_zero()
                    self.runtime.ledger[42]['identity'][field] = value
                with self.assertRaises(PermissionError):
                    self.dispatch(changed)

    def test_other_missing_resource_is_not_excused_by_game_exit(self):
        def factory():
            if self.factory.call_count == 2:
                self.exit_zero()
                raise PermissionError(13, 'witness denied', '/private/proof.utf16le')
            return {}
        self.factory.side_effect = factory
        with self.assertRaises(PermissionError):
            self.dispatch(mock.Mock())

    def test_existing_owner_errors_do_not_accept_terminal_disposition(self):
        self.factory.side_effect = [{}, self.denied()]
        def failed_graph():
            self.exit_zero()
            self.runtime.errors.append('Unrecorded adopted child reaped')
        with self.assertRaises(PermissionError):
            self.dispatch(failed_graph)

    def test_exit_does_not_waive_fixed_input_deadline(self):
        def late_exit():
            self.exit_zero()
            self.now = owner.INPUT
        with self.assertRaises(owner.Refusal):
            self.dispatch(late_exit)

    def test_failed_provider_cannot_use_terminal_disposition(self):
        def failed_provider():
            self.exit_zero()
            raise owner.Refusal('Maintained input provider failed')
        with self.assertRaises(owner.Refusal):
            self.dispatch(failed_provider)
        self.backend.reap.assert_not_called()

    def provider(self, status):
        child = mock.Mock(pid=43)
        self.runtime.ledger[43] = {'identity': {**self.identity, 'pid': 43}, 'pidfd': 143}
        self.runtime.spawn = mock.Mock(return_value=(child, {}))
        self.backend.reap.return_value = [(43, status)]
        with mock.patch.object(owner, 'file_proof', return_value={'sha256': 'a' * 64}):
            return self.runtime.run_input_provider(
                Path('/pinned/input.py'), 'a' * 64, Path('/private/guard.json'),
                ['click', '600', '450'], Path('/private'), mock.sentinel.log)

    def test_extracted_provider_preserves_exact_command_and_zero_wait(self):
        self.assertIsNone(self.provider(0))
        self.runtime.spawn.assert_called_once_with(
            [owner.sys.executable, '/pinned/input.py', '--guard', '/private/guard.json',
             'click', '600', '450'], cwd=Path('/private'), stdout=mock.sentinel.log)

    def test_extracted_provider_preserves_nonzero_rejection(self):
        with self.assertRaisesRegex(owner.Refusal, 'Maintained input provider failed'):
            self.provider(7)

    def test_extracted_provider_pin_mismatch_refuses_before_spawn(self):
        self.runtime.spawn = mock.Mock()
        with mock.patch.object(owner, 'file_proof', return_value={'sha256': 'b' * 64}):
            with self.assertRaisesRegex(owner.Refusal, 'Pinned maintained input provider changed'):
                self.runtime.run_input_provider(Path('/pinned/input.py'), 'a' * 64,
                    Path('/private/guard.json'), ['click', '600', '450'],
                    Path('/private'), mock.sentinel.log)
        self.runtime.spawn.assert_not_called()

    def test_extracted_provider_preserves_two_second_timeout_and_owned_signal(self):
        self.runtime.spawn = mock.Mock(return_value=(mock.Mock(pid=43), {}))
        self.runtime.signal_owned = mock.Mock()
        def timeout(_):
            self.now = 2
        with mock.patch.object(owner, 'file_proof', return_value={'sha256': 'a' * 64}), \
                mock.patch.object(owner.time, 'sleep', side_effect=timeout):
            with self.assertRaisesRegex(owner.Refusal, 'Input provider timeout'):
                self.runtime.run_input_provider(Path('/pinned/input.py'), 'a' * 64,
                    Path('/private/guard.json'), ['click', '600', '450'],
                    Path('/private'), mock.sentinel.log)
        self.runtime.signal_owned.assert_called_once_with(signal.SIGTERM)


if __name__ == '__main__':
    unittest.main()
