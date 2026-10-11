# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure ownership/gesture controls: no fork, prctl, Wine, Xvfb or XTest."""
import copy
import importlib.util
import json
from pathlib import Path
import signal
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('wine_owner', Path(__file__).with_name('nh_wine_fullgame_owner.py'))
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


class Backend:
    pid, uid, boot = 1, 1000, 'owned-boot'

    def __init__(self):
        self.tree = {1: {2}, 2: set()}
        self.identities = {2: {'pid': 2, 'start_ticks': 20, 'uid': self.uid, 'boot_id': self.boot,
                              'state': 'S', 'exe': '/pinned/wine', 'sha256': 'a' * 64, 'argv': ['wine', 'game']}}
        self.sent, self.waits, self.closed = [], [], []
        self.env = {'WINEPREFIX': '/private/prefix'}

    def children(self, pid): return self.tree.get(pid, set())
    def snapshot(self, pid):
        if pid not in self.identities: raise FileNotFoundError(pid)
        return copy.deepcopy(self.identities[pid])
    def environment(self, pid): return dict(self.env)
    def pidfd(self, pid): return pid + 100
    def close(self, fd): self.closed.append(fd)
    def send(self, fd, sig): self.sent.append((fd, sig))
    def reap(self):
        result, self.waits = self.waits, []
        return result


class Tests(unittest.TestCase):
    def setUp(self):
        self.backend = Backend()
        self.now = 0
        self.guard = mock.Mock()
        self.o = owner.Owner(self.backend, self.backend.env, self.guard, clock=lambda: self.now)
        self.o.started = 0
        self.o.armed.set()
        self.factory = lambda: {'fresh': True}
        self.verify = mock.Mock(return_value={'verified': True})

    def test_descendant_adopted_with_pidfd(self):
        self.assertEqual(self.o.adopt(), {2})
        self.assertEqual(self.o.ledger[2]['pidfd'], 102)

    def test_daemon_reparented_to_subreaper_is_adopted(self):
        self.o.adopt()
        self.backend.tree = {1: {3}, 3: set()}
        self.backend.identities[3] = dict(self.backend.identities[2], pid=3, start_ticks=30)
        self.o.adopt()
        self.assertEqual(set(self.o.ledger), {2, 3})

    def test_empty_wine_environment_does_not_abandon_cleanup(self):
        self.backend.env = {}
        self.o.adopt()
        self.assertTrue(self.o.ledger[2]['initial_environment_empty'])
        self.o.signal_owned(signal.SIGTERM)
        self.assertEqual(self.backend.sent, [(102, signal.SIGTERM)])

    def test_foreign_uid_never_adopted_or_signaled(self):
        self.backend.identities[2]['uid'] = 2000
        with self.assertRaises(owner.Refusal): self.o.adopt()
        self.assertFalse(self.backend.sent)

    def test_pid_reuse_no_signal(self):
        self.o.adopt()
        self.backend.identities[2]['start_ticks'] = 999
        self.o.signal_owned(signal.SIGKILL)
        self.assertFalse(self.backend.sent)
        self.assertTrue(self.o.errors)

    def test_boot_change_no_signal(self):
        self.o.adopt()
        self.backend.identities[2]['boot_id'] = 'other-boot'
        self.o.signal_owned(signal.SIGKILL)
        self.assertFalse(self.backend.sent)

    def test_executable_race_no_signal(self):
        self.o.adopt()
        original = self.backend.snapshot
        count = 0
        def raced(pid):
            nonlocal count
            count += 1
            result = original(pid)
            if count == 2: result['exe'] = '/unexpected/exec'
            return result
        self.backend.snapshot = raced
        self.o.signal_owned(signal.SIGKILL)
        self.assertFalse(self.backend.sent)

    def test_state_scheduling_change_not_pid_reuse(self):
        self.o.adopt()
        original = self.backend.snapshot
        count = 0
        def scheduled(pid):
            nonlocal count
            count += 1
            result = original(pid)
            result['state'] = 'R' if count % 2 else 'S'
            return result
        self.backend.snapshot = scheduled
        self.o.signal_owned(signal.SIGTERM)
        self.assertEqual(self.backend.sent, [(102, signal.SIGTERM)])

    def test_zombie_reaped_not_signaled(self):
        self.backend.identities[2]['state'] = 'Z'
        self.o.adopt(); self.o.signal_owned(signal.SIGTERM)
        self.backend.waits = [(2, 0)]
        self.o.reap()
        self.assertEqual(self.o.reaped, {2: 0})
        self.assertFalse(self.backend.sent)

    def test_one_reap_authority_updates_popen_status(self):
        self.o.adopt()
        process = mock.Mock(returncode=None)
        self.o.processes[2] = process
        self.backend.waits = [(2, 7)]
        self.o.reap(); self.o.reap()
        self.assertEqual(process.returncode, 7)
        self.assertEqual(self.o.reaped, {2: 7})
        process.wait.assert_not_called()

    def test_success_verifies_one_gesture(self):
        gesture = mock.Mock()
        self.o.verify_input(self.factory, self.verify, gesture)
        gesture.assert_called_once_with()
        self.guard.assert_called_once_with()
        self.assertEqual(self.verify.call_args.args[0]['age_after_io'], 0)

    def test_not_armed_no_gesture(self):
        self.o.armed.clear()
        with self.assertRaises(owner.Refusal): self.o.verify_input(self.factory, self.verify)
        self.verify.assert_not_called()

    def test_cutoff600_no_gesture(self):
        self.now = 600
        gesture = mock.Mock()
        with self.assertRaises(owner.Refusal): self.o.verify_input(self.factory, self.verify, gesture)
        gesture.assert_not_called()

    def test_delayed_proof_crosses_cutoff_no_gesture(self):
        def slow(): self.now = 600; return {}
        gesture = mock.Mock()
        with self.assertRaises(owner.Refusal): self.o.verify_input(slow, self.verify, gesture)
        gesture.assert_not_called()

    def test_deadline_crossing_gesture_no_success_ack(self):
        def late(): self.now = 600
        with self.assertRaises(owner.Refusal): self.o.verify_input(self.factory, self.verify, late)

    def test_finished_owner_rejects_input(self):
        self.o.finished.set()
        with self.assertRaises(owner.Refusal): self.o.verify_input(self.factory, self.verify)

    def test_verifier_refusal_no_input(self):
        self.verify.side_effect = owner.Refusal('missing actual env/maps')
        gesture = mock.Mock()
        with self.assertRaises(owner.Refusal): self.o.verify_input(self.factory, self.verify, gesture)
        gesture.assert_not_called()

    def test_proof_io_holds_no_watchdog_lock(self):
        def factory():
            self.assertFalse(self.o.lock._is_owned())
            return {}
        self.o.verify_input(factory, self.verify)

    def test_early_cleanup_not_global610_wait(self):
        self.o.adopt()
        self.backend.waits = [(2, 0)]
        self.o.cleanup()
        self.assertEqual(self.now, 0)
        self.assertFalse(self.o.errors)

    def test_cleanup_failure_bounded_local_grace(self):
        self.o.adopt()
        def advance(_): self.now += 1
        with mock.patch.object(owner.time, 'sleep', side_effect=advance): self.o.cleanup()
        self.assertEqual(self.now, 10)
        self.assertTrue(self.o.errors)
        self.assertIn((102, signal.SIGKILL), self.backend.sent)

    def test_watchdog_fixed_deadlines_without_process(self):
        self.o.adopt()
        class Ticks:
            def __init__(s): s.count = 0
            def wait(s, _):
                self.now = [605, 610, 640][s.count]; s.count += 1
                return False
            def set(s): pass
        self.o.finished = Ticks()
        self.o.watchdog()
        self.assertIn((102, signal.SIGTERM), self.backend.sent)
        self.assertIn((102, signal.SIGKILL), self.backend.sent)
        self.assertTrue(self.o.errors)

    def test_watchdog_does_not_call_blocking_proof_or_take_lock(self):
        self.o.adopt()
        self.backend.snapshot = mock.Mock(side_effect=AssertionError('metadata in timer'))
        self.backend.children = mock.Mock(side_effect=AssertionError('adoption in timer'))
        self.backend.reap = mock.Mock(side_effect=AssertionError('reap in timer'))
        self.o.lock = mock.Mock(side_effect=AssertionError('lock in timer'))
        class Ticks:
            def wait(s, _): self.now = 630; return False
            def set(s): pass
        self.o.finished = Ticks()
        self.o.watchdog()
        self.assertEqual(self.backend.sent, [(102, signal.SIGKILL)])
        self.backend.snapshot.assert_not_called()

    def test_deadline_uses_retained_lifetime_not_reused_numeric_pid(self):
        self.o.adopt()
        self.backend.identities[2]['start_ticks'] = 999
        self.o.deadline_signal(signal.SIGKILL)
        self.assertEqual(self.backend.sent, [(102, signal.SIGKILL)])

    def test_reap_unpublishes_pidfd_before_close(self):
        self.o.adopt(); self.backend.waits = [(2, 0)]; self.o.reap()
        self.o.deadline_signal(signal.SIGKILL)
        self.assertEqual(self.o.signal_fds, ())
        self.assertFalse(self.backend.sent)

    def test_unverified_direct_child_still_owned_for_cleanup(self):
        self.o.ledger[2] = {'identity': None, 'pidfd': 102}
        self.o.signal_owned(signal.SIGKILL)
        self.assertEqual(self.backend.sent, [(102, signal.SIGKILL)])

    def test_key_container_refuses_typed(self):
        for value in ([], {}, None, 1):
            with self.assertRaises(owner.Refusal):
                owner.input_argv({'id': 1, 'action': 'key', 'key': value})

    def test_new_direct_lifetime_clears_old_numeric_pid_status(self):
        self.o.adopt(); self.backend.waits = [(2, 7)]; self.o.reap()
        previous = copy.deepcopy(self.o.ledger[2])
        self.o.retain(2, {'identity': dict(previous['identity'], start_ticks=999), 'pidfd': 202})
        self.assertNotIn(2, self.o.reaped)
        self.assertEqual(self.o.signal_fds, (202,))
        self.assertEqual(self.o.history[0]['status'], 7)
        self.assertEqual(self.o.history[0]['entry'], previous)
        self.o.deadline_signal(signal.SIGKILL)
        self.assertEqual(self.backend.sent, [(202, signal.SIGKILL)])

    def test_new_adopted_lifetime_clears_old_numeric_pid_status(self):
        self.o.adopt(); self.backend.waits = [(2, 0)]; self.o.reap()
        self.backend.identities[2]['start_ticks'] = 999
        self.backend.pidfd = lambda pid: 202
        self.o.adopt()
        self.assertNotIn(2, self.o.reaped)
        self.assertEqual(self.o.ledger[2]['identity']['start_ticks'], 999)
        self.assertEqual(self.o.signal_fds, (202,))

    def test_cleanup_exception_still_stops_timer_reaps_and_returns_for_receipt(self):
        self.o.adopt(); self.backend.waits = [(2, 0)]
        self.o.cleanup = mock.Mock(side_effect=owner.Refusal('adoption failed'))
        supervisor = mock.Mock(); supervisor.is_alive.return_value = False
        owner.finish_owner(self.o, supervisor)
        self.assertTrue(self.o.finished.is_set())
        supervisor.join.assert_called_once_with(timeout=1)
        self.assertEqual(self.o.reaped, {2: 0})
        self.assertEqual(self.backend.closed, [102])
        self.assertIn('Cleanup exception: adoption failed', self.o.errors)
        self.assertIn((102, signal.SIGKILL), self.backend.sent)

    def test_live_watchdog_prevents_pidfd_close_after_cleanup_exception(self):
        self.o.adopt(); self.backend.waits = [(2, 0)]
        self.o.cleanup = mock.Mock(side_effect=RuntimeError('cleanup'))
        supervisor = mock.Mock(); supervisor.is_alive.return_value = True
        owner.finish_owner(self.o, supervisor)
        self.assertFalse(self.backend.closed)
        self.assertTrue(any('pidfds retained' in row for row in self.o.errors))

    def test_logical_click_and_key_no_batch(self):
        self.assertEqual(owner.input_argv({'id': 1, 'action': 'click', 'x': 1279, 'y': 799}), ['click', '1279', '799'])
        self.assertEqual(owner.input_argv({'id': 2, 'action': 'key', 'key': 'Escape'}), ['key', 'Escape'])
        for request in ({'id': 1, 'action': 'text', 'text': 'abc'}, {'id': 1, 'action': 'click', 'x': 1, 'y': 2, 'then': 'key'},
                        {'id': 1, 'action': 'move', 'x': 1280, 'y': 0}, {'id': 1, 'action': 'move', 'x': True, 'y': 0},
                        {'id': 1, 'action': 'key', 'key': 'F12'}, {'id': True, 'action': 'key', 'key': 'Return'}):
            with self.assertRaises(owner.Refusal): owner.input_argv(request)

    def test_file_proof_symlink_refused(self):
        with tempfile.TemporaryDirectory() as name:
            p = Path(name); (p/'file').write_bytes(b'owned'); (p/'link').symlink_to(p/'file')
            with self.assertRaises(OSError): owner.file_proof(p/'link')

    def test_old_artifact_refused_before_any_fork(self):
        with tempfile.TemporaryDirectory() as name:
            pe = Path(name)/'new-horizons.exe'; pe.write_bytes(b'MZ old artifact')
            with mock.patch.object(owner.subprocess, 'Popen') as popen:
                with self.assertRaises(owner.Refusal):
                    owner.run_staged_fullgame({'executable': str(pe)}, mock.Mock(), {},
                                             verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
                popen.assert_not_called()

    def test_admitted_input_hash_required_before_fork(self):
        with tempfile.TemporaryDirectory() as name:
            pe = Path(name)/'new-horizons.exe'
            pe.write_bytes('NH_WINE_PRE_SDL_V1 NH_PRIVATE_WINE_NONCE'.encode('utf-16-le'))
            plan = {'executable': str(pe), 'input_program': str(Path(owner.__file__).with_name('nh-private-input.py').resolve()),
                    'input_sha256': '0' * 64}
            with mock.patch.object(owner.subprocess, 'Popen') as popen:
                with self.assertRaisesRegex(owner.Refusal, 'admitted pin'):
                    owner.run_staged_fullgame(plan, mock.Mock(), {}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
                popen.assert_not_called()

    def test_launch_binding_cannot_be_reminted(self):
        with tempfile.TemporaryDirectory() as name:
            pe = Path(name)/'new-horizons.exe'
            pe.write_bytes('NH_WINE_PRE_SDL_V1 NH_PRIVATE_WINE_NONCE'.encode('utf-16-le'))
            with mock.patch.object(owner.subprocess, 'Popen') as popen:
                with self.assertRaisesRegex(owner.Refusal, 'second launch'):
                    owner.run_staged_fullgame({'executable': str(pe), 'launch_nonce': 'a'*64}, mock.Mock(), {},
                                       verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
                popen.assert_not_called()

    def test_five_game_directories_precreated_before_backend_or_fork(self):
        with tempfile.TemporaryDirectory() as name:
            base = Path(name); root = base/'private'; root.mkdir(mode=0o700)
            pe = base/'new-horizons.exe'
            pe.write_bytes('NH_WINE_PRE_SDL_V1 NH_PRIVATE_WINE_NONCE'.encode('utf-16-le'))
            names = ('VCMI_lib.dll', 'SDL3.dll', 'SDL3_image.dll', 'SDL3_mixer.dll', 'SDL3_ttf.dll')
            for item in names: (base/item).write_bytes(b'pure file fixture')
            wine = base/'wine'; wine.write_bytes(b'pure wine fixture')
            server = base/'server'; server.write_bytes(b'pure server fixture')
            program = Path(owner.__file__).with_name('nh-private-input.py').resolve()
            plan = {'executable': str(pe), 'root': str(root), 'content': {}, 'input_program': str(program),
                    'input_sha256': owner.file_proof(program)['sha256'],
                    'executable_sha256': owner.file_proof(pe)['sha256'],
                    'dlls': {item: owner.file_proof(base/item)['sha256'] for item in names},
                    'wine': str(wine), 'wine_sha256': owner.file_proof(wine)['sha256'],
                    'server': str(server), 'server_sha256': owner.file_proof(server)['sha256']}
            profile = mock.Mock()
            def staged(content, destination):
                for item in ('HeroesIII-NewHorizons/content', 'HeroesIII-NewHorizons/config',
                             'HeroesIII-NewHorizons/cache', 'HeroesIII-NewHorizons/logs', 'new-horizons/Saves'):
                    self.assertTrue((destination/'localappdata'/item).is_dir())
            profile.stage_content.side_effect = staged
            def backend():
                profile.stage_content.assert_called_once_with({}, root)
                profile.verify_staged_content.assert_called_once_with({}, root, verify_copy_bytes=True)
                raise owner.Refusal('pure stop before backend')
            with mock.patch.object(owner, 'Linux', side_effect=backend), \
                 mock.patch.object(owner.subprocess, 'Popen') as popen:
                with self.assertRaisesRegex(owner.Refusal, 'pure stop'):
                    owner.run_staged_fullgame(plan, profile, {}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
                popen.assert_not_called()
            for item in ('HeroesIII-NewHorizons/content', 'HeroesIII-NewHorizons/config',
                         'HeroesIII-NewHorizons/cache', 'HeroesIII-NewHorizons/logs', 'new-horizons/Saves'):
                path = root/'localappdata'/item
                self.assertTrue(path.is_dir())
                self.assertEqual(path.stat().st_mode & 0o777, 0o700)

    def test_missing_content_refuses_before_module_backend_or_fork(self):
        with mock.patch.object(owner, 'content_module') as module, \
             mock.patch.object(owner, 'Linux') as backend, \
             mock.patch.object(owner.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(owner.Refusal, 'manifest missing'):
                owner.run_fullgame({}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
            module.assert_not_called(); backend.assert_not_called(); popen.assert_not_called()

    def test_final_original_and_immutable_verification_on_runtime_refusal(self):
        with tempfile.TemporaryDirectory() as name:
            profile = mock.Mock(); plan = {'content': {'admitted': True}, 'root': name}
            def runtime(plan, module, state, **kwargs):
                state['staged'] = True
                raise owner.Refusal('retained runtime refusal')
            with mock.patch.object(owner, 'content_module', return_value=profile), \
                 mock.patch.object(owner, 'run_staged_fullgame', side_effect=runtime):
                with self.assertRaisesRegex(owner.Refusal, 'retained runtime refusal'):
                    owner.run_fullgame(plan, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
            self.assertEqual(profile.verify_original_content.call_count, 2)
            profile.verify_staged_content.assert_called_once_with(plan['content'], Path(name), verify_copy_bytes=False)
            self.assertTrue((Path(name)/'content-final.json').is_file())

    def test_failed_initial_original_admission_still_verifies_final_originals(self):
        with tempfile.TemporaryDirectory() as name:
            profile = mock.Mock(); profile.verify_original_content.side_effect = [owner.Refusal('bad original'), None]
            with mock.patch.object(owner, 'content_module', return_value=profile), \
                 mock.patch.object(owner, 'run_staged_fullgame') as runtime:
                with self.assertRaisesRegex(owner.Refusal, 'bad original'):
                    owner.run_fullgame({'content': {}, 'root': name}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
                runtime.assert_not_called()
            self.assertEqual(profile.verify_original_content.call_count, 2)
            profile.verify_staged_content.assert_not_called()
            receipt = json.loads((Path(name)/'content-final.json').read_text())
            self.assertFalse(receipt['initial_originals_verified'])
            self.assertTrue(receipt['final_originals_verified'])
            self.assertFalse(receipt['originals_unchanged'])

    def test_final_original_failure_cannot_be_reported_success(self):
        with tempfile.TemporaryDirectory() as name:
            profile = mock.Mock(); profile.verify_original_content.side_effect = [None, owner.Refusal('changed original')]
            with mock.patch.object(owner, 'content_module', return_value=profile), \
                 mock.patch.object(owner, 'run_staged_fullgame', return_value=0):
                with self.assertRaisesRegex(owner.Refusal, 'Final content'):
                    owner.run_fullgame({'content': {}, 'root': name}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)


if __name__ == '__main__':
    unittest.main()
