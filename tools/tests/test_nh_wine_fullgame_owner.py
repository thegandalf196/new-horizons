# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure ownership/gesture controls: no fork, prctl, Wine, Xvfb or XTest."""
import copy
import importlib.util
import json
import os
import stat
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
                              'state': 'S', 'ppid': 1, 'exe': '/pinned/wine', 'sha256': 'a' * 64, 'argv': ['wine', 'game']}}
        self.sent, self.waits, self.closed = [], [], []
        self.exited_fds = set()
        self.env = {'WINEPREFIX': '/private/prefix'}

    def children(self, pid): return self.tree.get(pid, set())
    def snapshot(self, pid):
        if pid not in self.identities: raise FileNotFoundError(pid)
        return copy.deepcopy(self.identities[pid])
    def environment(self, pid): return dict(self.env)
    def kernel_snapshot(self, pid):
        if pid == self.pid:
            return {'pid': 1, 'start_ticks': 10, 'uid': self.uid, 'boot_id': self.boot, 'state': 'S', 'ppid': 0}
        if pid not in self.identities: raise FileNotFoundError(pid)
        return {key: self.identities[pid][key] for key in ('pid', 'start_ticks', 'uid', 'boot_id', 'state', 'ppid')}
    def pidfd(self, pid): return pid + 100
    def exited(self, fd): return fd in self.exited_fds
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

    def kernel_read(self, after_start, after_parent):
        # Exercise the actual reader with synthetic stat contents, not /proc.
        def contents(start, parent):
            fields = ['0'] * 20
            fields[0], fields[1], fields[19] = 'S', str(parent), str(start)
            return '23 (private name) ' + ' '.join(fields)
        backend = object.__new__(owner.Linux)
        backend.boot = 'synthetic-boot'
        with mock.patch.object(Path, 'read_text', side_effect=[contents(100, 7), contents(after_start, after_parent)]), \
             mock.patch.object(Path, 'stat', return_value=mock.Mock(st_uid=1000)):
            return backend.kernel_snapshot(23)

    def test_kernel_identity_lifetime_change_remains_refused_with_numeric_diagnostic(self):
        with self.assertRaises(owner.Refusal) as caught:
            self.kernel_read(101, 7)
        self.assertEqual(str(caught.exception), 'Process changed during kernel identity read: '
            'kind=lifetime pid=23 before_start_ticks=100 after_start_ticks=101 before_ppid=7 after_ppid=7')

    def test_kernel_identity_ancestry_change_remains_refused_with_numeric_diagnostic(self):
        with self.assertRaises(owner.Refusal) as caught:
            self.kernel_read(100, 8)
        self.assertEqual(str(caught.exception), 'Process changed during kernel identity read: '
            'kind=ancestry pid=23 before_start_ticks=100 after_start_ticks=100 before_ppid=7 after_ppid=8')

    def test_kernel_identity_both_changes_remain_refused_with_numeric_diagnostic(self):
        with self.assertRaises(owner.Refusal) as caught:
            self.kernel_read(101, 8)
        self.assertEqual(str(caught.exception), 'Process changed during kernel identity read: '
            'kind=both pid=23 before_start_ticks=100 after_start_ticks=101 before_ppid=7 after_ppid=8')

    def test_kernel_identity_unchanged_read_preserves_normal_result(self):
        self.assertEqual(self.kernel_read(100, 7), dict(pid=23, start_ticks=100, uid=1000,
            boot_id='synthetic-boot', state='S', ppid=7))

    def test_descendant_adopted_with_pidfd(self):
        self.assertEqual(self.o.adopt(), {2})
        self.assertEqual(self.o.ledger[2]['pidfd'], 102)

    def test_absent_descendant_without_ready_pidfd_is_not_parent_reaped(self):
        self.o.adopt();self.backend.identities.pop(2);self.o.reap()
        self.assertFalse(self.o.parent_reaped)
        self.assertFalse(self.o.graph_drained())

    def test_ready_absent_descendant_has_separate_proof_not_exit_status(self):
        self.o.adopt();self.backend.exited_fds.add(102);self.backend.identities.pop(2);self.o.reap()
        self.assertTrue(self.o.graph_drained())
        self.assertEqual(self.o.reaped,{})
        self.assertEqual(self.o.parent_reaped[2]['proof'],'retained-pidfd-exited-and-absent')
        self.assertEqual(self.o.signal_fds,())

    def test_live_or_zombie_same_identity_is_not_parent_reaped(self):
        self.o.adopt();self.backend.exited_fds.add(102)
        for state in ('S','Z'):
            self.backend.identities[2]['state']=state;self.o.reap()
            self.assertFalse(self.o.graph_drained())
            self.assertFalse(self.o.parent_reaped)

    def test_ready_old_pidfd_and_reused_pid_finishes_only_old_lifetime(self):
        self.o.adopt();self.backend.exited_fds.add(102)
        original=copy.deepcopy(self.o.ledger[2]['identity'])
        self.backend.identities[2]['start_ticks']=999
        self.backend.identities[2]['ppid']=99
        self.backend.tree={1:set()}
        self.o.reap()
        self.assertEqual(self.o.parent_reaped[2]['identity'],original)
        self.assertEqual(self.o.parent_reaped[2]['proof'],'retained-pidfd-exited-and-numeric-pid-reused')
        self.o.signal_owned(signal.SIGTERM)
        self.assertFalse(self.backend.sent)

    def test_unreadable_descendant_is_not_parent_reaped(self):
        self.o.adopt();self.backend.exited_fds.add(102)
        with mock.patch.object(self.backend,'kernel_snapshot',side_effect=PermissionError()):self.o.reap()
        self.assertFalse(self.o.parent_reaped)

    def test_pidfd_readiness_requires_exit_not_error_or_invalid_fd(self):
        backend=owner.Linux.__new__(owner.Linux)
        with mock.patch.object(owner.select,'poll') as factory:
            for flags in (owner.select.POLLIN,owner.select.POLLHUP):
                factory.return_value.poll.return_value=[(102,flags)]
                self.assertTrue(backend.exited(102))
            factory.return_value.poll.return_value=[]
            self.assertFalse(backend.exited(102))
            for flags in (owner.select.POLLERR,owner.select.POLLNVAL,owner.select.POLLIN|owner.select.POLLERR):
                factory.return_value.poll.return_value=[(102,flags)]
                with self.assertRaises(owner.Refusal):backend.exited(102)

    def test_direct_process_requires_actual_wait_status_even_when_absent(self):
        self.o.adopt();process=mock.Mock();self.o.processes[2]=process
        self.backend.exited_fds.add(102);self.backend.identities.pop(2);self.o.reap()
        self.assertFalse(self.o.graph_drained())
        self.assertFalse(self.o.parent_reaped)
        self.backend.waits=[(2,86)];self.o.reap()
        self.assertTrue(self.o.graph_drained())
        self.assertEqual(process.returncode,86)
        self.assertEqual(self.o.reaped[2],86)

    def test_bootstrap_selects_only_owned_game_with_complete_exact_mappings(self):
        self.o.adopt()
        self.backend.tree[2] = {3}
        self.backend.identities[3] = {**self.backend.identities[2], 'pid':3, 'start_ticks':30, 'ppid':2}
        self.o.adopt()
        expected = [dict(path='/selected/'+name,sha256='a'*64,inode=i+1,device='00:01')
                    for i,name in enumerate(('new-horizons.exe','VCMI_lib.dll','SDL3.dll','SDL3_image.dll','SDL3_mixer.dll','SDL3_ttf.dll'))]
        self.backend.mappings = mock.Mock(return_value=expected)
        self.assertEqual(owner.owned_game_candidates(self.o,2,expected),[self.backend.identities[3]])
        self.backend.mappings.return_value = expected[:-1]
        self.assertEqual(owner.owned_game_candidates(self.o,2,expected),[])
        self.backend.mappings.return_value = [*expected,{**expected[0],'path':'/other/new-horizons.exe'}]
        self.assertEqual(owner.owned_game_candidates(self.o,2,expected),[])
        self.backend.mappings.return_value = [*expected,{**expected[0],'sha256':'b'*64}]
        self.assertEqual(owner.owned_game_candidates(self.o,2,expected),[])
        # A process outside the retained kernel graph is never inspected.
        self.backend.identities[4] = {**self.backend.identities[3], 'pid':4, 'ppid':99}
        self.assertNotIn(4,[call.args[0] for call in self.backend.mappings.call_args_list])

    def test_bootstrap_child_reuse_and_ambiguity_refuse_without_losing_cleanup(self):
        self.o.adopt()
        self.backend.tree[2] = {3,4}
        for pid in (3,4):
            self.backend.identities[pid] = {**self.backend.identities[2],'pid':pid,'start_ticks':pid*10,'ppid':2}
        self.o.adopt()
        expected=[dict(path='/selected/new-horizons.exe',sha256='a'*64,inode=1,device='00:01')]
        self.backend.mappings=mock.Mock(return_value=expected)
        with self.assertRaisesRegex(owner.Refusal,'Multiple'):
            owner.owned_game_candidates(self.o,2,expected)
        self.backend.identities[3]['start_ticks']=999
        with self.assertRaisesRegex(owner.Refusal,'changed'):
            owner.owned_game_candidates(self.o,2,expected)
        self.assertEqual(set(self.o.ledger),{2,3,4})
        self.assertEqual(len(self.o.signal_fds),3)

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

    def test_cleanup_does_not_require_executable_visibility(self):
        self.o.adopt()
        self.backend.snapshot = mock.Mock(side_effect=PermissionError('exe unreadable'))
        self.o.signal_owned(signal.SIGKILL)
        self.assertEqual(self.backend.sent, [(102, signal.SIGKILL)])
        self.backend.snapshot.assert_not_called()

    def test_unreadable_environment_retained_before_diagnostics_and_reaped(self):
        def denied(pid):
            self.assertIn(pid, self.o.ledger)
            self.assertIn(102, self.o.signal_fds)
            raise PermissionError(13, 'environment unreadable')
        self.backend.environment = denied
        self.o.adopt()
        entry = self.o.ledger[2]
        self.assertIsNone(entry['initial_environment_empty'])
        self.assertEqual(entry['environment_diagnostic'], {'status': 'unreadable', 'error_type': 'PermissionError', 'errno': 13})
        self.o.signal_owned(signal.SIGTERM)
        self.backend.waits = [(2, 0)]
        self.o.reap()
        self.assertEqual(self.backend.sent, [(102, signal.SIGTERM)])
        self.assertEqual(self.o.reaped, {2: 0})
        self.assertFalse(self.o.errors)

    def test_unreadable_actual_input_environment_still_refuses_gesture(self):
        self.backend.environment = mock.Mock(side_effect=PermissionError(13, 'actual environment'))
        self.o.adopt()
        gesture = mock.Mock()
        with self.assertRaises(PermissionError):
            self.o.verify_input(lambda: self.backend.environment(2), self.verify, gesture)
        gesture.assert_not_called()
        self.verify.assert_not_called()

    def test_foreign_actual_parent_never_retained(self):
        self.backend.identities[2]['ppid'] = 999
        with self.assertRaises(owner.Refusal): self.o.adopt()
        self.assertFalse(self.o.ledger)
        self.assertFalse(self.backend.sent)

    def test_pidfd_race_closes_unretained_fd(self):
        original = self.backend.pidfd
        def raced(pid):
            fd = original(pid)
            self.backend.identities[pid]['start_ticks'] += 1
            return fd
        self.backend.pidfd = raced
        with self.assertRaises(owner.Refusal): self.o.adopt()
        self.assertEqual(self.backend.closed, [102])
        self.assertFalse(self.o.ledger)

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
                state = {}
                with self.assertRaisesRegex(owner.Refusal, 'pure stop'):
                    owner.run_staged_fullgame(plan, profile, state, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
                self.addCleanup(state['mapping'].close)  # Direct inner-fixture call lacks outer finalizer.
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


class WineRootMappingTests(unittest.TestCase):
    """Synthetic filesystem only; never forks or invokes Wine."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.root.chmod(0o700)
        (self.root / 'prefix').mkdir(mode=0o700)

    def mapping(self):
        result = owner.WineRootMapping(self.root)
        self.addCleanup(result.close)
        return result

    def test_missing_c_drive_is_created_private_and_canonical(self):
        self.assertFalse((self.root / 'prefix/drive_c').exists())
        mapping = self.mapping()
        self.assertEqual(os.readlink(self.root / 'prefix/dosdevices/z:'), '/')
        self.assertEqual(os.readlink(self.root / 'prefix/dosdevices/c:'), '../drive_c')
        self.assertEqual(stat.S_IMODE((self.root / 'prefix/drive_c').stat().st_mode), 0o700)
        self.assertEqual((self.root / 'prefix/dosdevices/c:').resolve(), self.root / 'prefix/drive_c')
        mapping.verify()
        self.assertEqual(len(mapping.fds), 4)

    def test_c_drive_link_replacement_and_uppercase_refused(self):
        mapping = self.mapping()
        link = self.root / 'prefix/dosdevices/c:'
        link.unlink(); link.symlink_to('/')
        with self.assertRaises(owner.Refusal):mapping.verify()

    def test_c_drive_uppercase_conflict_refused(self):
        mapping = self.mapping()
        (self.root / 'prefix/dosdevices/C:').symlink_to('../drive_c')
        with self.assertRaises(owner.Refusal):mapping.verify()

    def test_c_drive_retarget_refused_even_with_same_link_identity(self):
        mapping = self.mapping()
        readlink = os.readlink
        def changed(path, **kwargs):
            return '/' if path == 'c:' else readlink(path, **kwargs)
        with mock.patch.object(owner.os,'readlink',side_effect=changed):
            with self.assertRaises(owner.Refusal):mapping.verify()

    def test_c_drive_directory_replacement_refused(self):
        mapping = self.mapping()
        drive = self.root / 'prefix/drive_c'
        drive.rename(self.root / 'prefix/old-drive_c')
        drive.mkdir(mode=0o700)
        with self.assertRaises(owner.Refusal):mapping.verify()

    def test_c_drive_symlink_replacement_refused(self):
        mapping = self.mapping()
        drive = self.root / 'prefix/drive_c'
        drive.rename(self.root / 'prefix/old-drive_c')
        drive.symlink_to(self.root / 'prefix/old-drive_c')
        with self.assertRaises(owner.Refusal):mapping.verify()

    def test_missing_c_drive_link_refused(self):
        mapping = self.mapping()
        (self.root / 'prefix/dosdevices/c:').unlink()
        with self.assertRaises(owner.Refusal):mapping.verify()

    def test_preexisting_c_directory_or_symlink_refused_without_overwrite(self):
        drive = self.root / 'prefix/drive_c'
        drive.symlink_to(self.root)
        with self.assertRaises(owner.Refusal):owner.WineRootMapping(self.root)
        self.assertTrue(drive.is_symlink())
        drive.unlink(); drive.mkdir(mode=0o700)
        with self.assertRaises(owner.Refusal):owner.WineRootMapping(self.root)
        self.assertTrue(drive.is_dir())

    def test_non_root_link_target_refused_with_same_link_identity(self):
        mapping = self.mapping()
        with mock.patch.object(owner.os, 'readlink', return_value='/not-root'):
            with self.assertRaises(owner.Refusal):
                mapping.verify()

    def test_replacement_root_target_still_refused(self):
        mapping = self.mapping()
        link = self.root / 'prefix/dosdevices/z:'
        link.rename(link.with_name('retained-old-z'))
        link.symlink_to('/')
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_missing_link_refused(self):
        mapping = self.mapping()
        (self.root / 'prefix/dosdevices/z:').unlink()
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_uppercase_drive_conflict_refused(self):
        mapping = self.mapping()
        (self.root / 'prefix/dosdevices/Z:').symlink_to('/')
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_parent_name_replacement_refused(self):
        mapping = self.mapping()
        (self.root / 'prefix/dosdevices').rename(self.root / 'prefix/old-dosdevices')
        (self.root / 'prefix/dosdevices').mkdir(mode=0o700)
        (self.root / 'prefix/dosdevices/z:').symlink_to('/')
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_prefix_name_replacement_refused(self):
        mapping = self.mapping()
        (self.root / 'prefix').rename(self.root / 'old-prefix')
        (self.root / 'prefix').mkdir(mode=0o700)
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_root_mode_change_refused(self):
        mapping = self.mapping()
        self.root.chmod(0o755)
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_uid_mismatch_refused_before_link_creation(self):
        with mock.patch.object(owner.os, 'getuid', return_value=os.getuid() + 1):
            with self.assertRaises(owner.Refusal):
                owner.WineRootMapping(self.root)
        self.assertFalse((self.root / 'prefix/dosdevices').exists())

    def test_bad_fd_or_unreadable_mapping_refused(self):
        mapping = self.mapping()
        with mock.patch.object(owner.os, 'fstat', side_effect=OSError('synthetic unreadable')):
            with self.assertRaises(owner.Refusal):
                mapping.verify()

    def test_link_uid_mismatch_refused(self):
        mapping = self.mapping()
        actual = os.stat
        def changed(path, *args, **kwargs):
            info = actual(path, *args, **kwargs)
            if path == 'z:':
                values = list(info)
                values[4] += 1
                return os.stat_result(values)
            return info
        with mock.patch.object(owner.os, 'stat', side_effect=changed):
            with self.assertRaises(owner.Refusal):
                mapping.verify()

    def test_held_fd_mismatch_refused(self):
        mapping = self.mapping()
        mapping.parents[1] = mapping.parents[0]
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def test_constructor_error_closes_all_opened_fds(self):
        real_close = os.close
        with mock.patch.object(owner.os, 'symlink', side_effect=OSError('synthetic failure')), \
             mock.patch.object(owner.os, 'close', wraps=real_close) as closed:
            with self.assertRaises(owner.Refusal):
                owner.WineRootMapping(self.root)
            self.assertEqual(closed.call_count, 4)

    def test_close_is_idempotent_and_closed_guard_refuses(self):
        mapping = self.mapping()
        retained = list(mapping.fds)
        mapping.close(); mapping.close()
        for fd in retained:
            with self.assertRaises(OSError):
                os.fstat(fd)
        with self.assertRaises(owner.Refusal):
            mapping.verify()

    def failing_close(self, mapping):
        real_close = os.close
        attempts = []
        def close(fd):
            attempts.append(fd)
            real_close(fd)
            if len(attempts) == 1:
                raise OSError('synthetic first close failure')
        return close, attempts

    def test_close_failure_attempts_every_retained_fd_once(self):
        mapping = self.mapping()
        retained = list(mapping.fds)
        close, attempts = self.failing_close(mapping)
        with mock.patch.object(owner.os, 'close', side_effect=close):
            with self.assertRaisesRegex(owner.Refusal, 'handle cleanup failed'):
                mapping.close()
        self.assertEqual(attempts, list(reversed(retained)))
        self.assertEqual(mapping.fds, [])
        mapping.close()  # No retry of any descriptor, including the failed close.

    def test_constructor_cleanup_failure_preserves_initial_cause(self):
        real_close = os.close
        attempts = []
        original = OSError('synthetic link failure')
        def close(fd):
            attempts.append(fd)
            real_close(fd)
            if len(attempts) == 1:
                raise OSError('synthetic close failure')
        with mock.patch.object(owner.os, 'symlink', side_effect=original), \
             mock.patch.object(owner.os, 'close', side_effect=close):
            with self.assertRaisesRegex(owner.Refusal, 'handle cleanup failed') as caught:
                owner.WineRootMapping(self.root)
        self.assertIs(caught.exception.__cause__, original)
        self.assertEqual(len(attempts), 4)
        self.assertEqual(len(set(attempts)), 4)

    def test_outer_close_failure_retains_runtime_failure_in_final_receipt(self):
        mapping = self.mapping()
        close, attempts = self.failing_close(mapping)
        def runtime(plan, module, state, **kwargs):
            state['mapping'] = mapping
            raise owner.Refusal('synthetic primary runtime refusal')
        with mock.patch.object(owner, 'content_module', return_value=mock.Mock()), \
             mock.patch.object(owner, 'run_staged_fullgame', side_effect=runtime), \
             mock.patch.object(owner.os, 'close', side_effect=close):
            with self.assertRaisesRegex(owner.Refusal, 'Final content') as caught:
                owner.run_fullgame({'root': str(self.root), 'content': {}}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
        self.assertEqual(str(caught.exception.__cause__), 'synthetic primary runtime refusal')
        self.assertEqual(len(attempts), 4)
        self.assertEqual(mapping.fds, [])
        receipt = json.loads((self.root / 'content-final.json').read_text())
        self.assertEqual(receipt['runtime_failure'], 'synthetic primary runtime refusal')
        self.assertIn('Final Wine root mapping handle cleanup failed', receipt['final_errors'])

    def test_runtime_callback_bracket_rejects_mid_proof_link_change(self):
        mapping = self.mapping()
        def changed(context):
            (self.root / 'prefix/dosdevices/z:').unlink()
            return {'verified': True}
        with self.assertRaises(owner.Refusal):
            mapping.verify_runtime(changed, {})

    def test_runtime_callback_has_before_and_after_mapping_checks(self):
        mapping = self.mapping()
        with mock.patch.object(mapping, 'verify', wraps=mapping.verify) as checked:
            self.assertEqual(mapping.verify_runtime(lambda context: context, {'actual': True}), {'actual': True})
            self.assertEqual(checked.call_count, 2)

    def test_outer_final_check_keeps_handles_until_after_audit(self):
        mapping = self.mapping()
        profile = mock.Mock()
        plan = {'root': str(self.root), 'content': {}}
        def runtime(plan, module, state, **kwargs):
            state['mapping'] = mapping
            raise owner.Refusal('synthetic runtime failure')
        def originals(content):
            self.assertEqual(len(mapping.fds), 4)
        profile.verify_original_content.side_effect = originals
        with mock.patch.object(owner, 'content_module', return_value=profile), \
             mock.patch.object(owner, 'run_staged_fullgame', side_effect=runtime):
            with self.assertRaisesRegex(owner.Refusal, 'synthetic runtime failure'):
                owner.run_fullgame(plan, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
        self.assertEqual(mapping.fds, [])

    def test_final_mapping_refusal_is_retained_and_handles_closed(self):
        mapping = self.mapping()
        def runtime(plan, module, state, **kwargs):
            state['mapping'] = mapping
            (self.root / 'prefix/dosdevices/z:').unlink()
            return 0
        with mock.patch.object(owner, 'content_module', return_value=mock.Mock()), \
             mock.patch.object(owner, 'run_staged_fullgame', side_effect=runtime):
            with self.assertRaisesRegex(owner.Refusal, 'Final content'):
                owner.run_fullgame({'root': str(self.root), 'content': {}}, verify_xvfb=lambda _: None, verify_runtime=lambda _: None)
        self.assertEqual(mapping.fds, [])
        receipt = json.loads((self.root / 'content-final.json').read_text())
        self.assertIn('Final Wine root mapping verification failed', receipt['final_errors'])


class TypedFixtureContractTest(unittest.TestCase):
    def plan(self, name='playback-cabir.vmap'):
        return {'wine': '/pinned/wine', 'executable': '/selected/new-horizons.exe',
                'fixture_map': name, 'content': {'map': {'filename': name}}}

    def test_fixed_direct_and_bootstrap_arguments(self):
        plan = self.plan()
        fixed = ['--testmap', 'Maps/playback-cabir.vmap', '--spectate', '--savefrequency', '0']
        self.assertEqual(owner.fixture_arguments(plan), fixed)
        self.assertEqual(owner.fullgame_command(plan), ['/pinned/wine', '/selected/new-horizons.exe', *fixed])
        plan['bootstrap'] = '/private/nh-wine-bootstrap.exe'
        self.assertEqual(owner.fullgame_command(plan), ['/pinned/wine', plan['bootstrap'], '--fixture-map', 'playback-cabir.vmap'])
        self.assertEqual(owner.fixture_arguments(plan), fixed)

    def test_legacy_has_no_added_arguments(self):
        plan = self.plan()
        del plan['fixture_map']
        self.assertEqual(owner.fullgame_command(plan), ['/pinned/wine', '/selected/new-horizons.exe'])
        plan['bootstrap'] = '/private/nh-wine-bootstrap.exe'
        self.assertEqual(owner.fullgame_command(plan), ['/pinned/wine', plan['bootstrap']])

    def test_invalid_names_and_wrong_manifest_refuse(self):
        for name in ('', True, 'Maps/a.vmap', '../a.vmap', 'a b.vmap', 'a.vmap"',
                     'a.h3m', 'a.VMAP', 'CON.vmap', 'lpt9.vmap', 'com1.vmap', 'a'*65+'.vmap', 'é.vmap'):
            with self.subTest(name=name), self.assertRaises(owner.Refusal):
                owner.fixture_arguments(self.plan(name))
        plan = self.plan()
        plan['content']['map']['filename'] = 'different.vmap'
        with self.assertRaises(owner.Refusal): owner.fixture_arguments(plan)

    def test_exact_actual_game_argv_spellings(self):
        plan = self.plan()
        fixed = owner.fixture_arguments(plan)
        for executable in (plan['executable'], 'Z:\\selected\\new-horizons.exe'):
            for prefix in ([], [plan['wine']]):
                owner.verify_fixture_argv(plan, {'argv': [*prefix, executable, *fixed]})

    def test_extra_reordered_wrong_target_and_missing_argv_refuse(self):
        plan = self.plan()
        fixed = owner.fixture_arguments(plan)
        for argv in (None, [], ['/foreign/new-horizons.exe', *fixed],
                     [plan['executable'], *fixed, '--onlyAI'], [plan['executable'], *reversed(fixed)],
                     [plan['executable'], '--testmap', 'Maps/foreign.vmap', *fixed[2:]]):
            with self.subTest(argv=argv), self.assertRaises(owner.Refusal):
                owner.verify_fixture_argv(plan, {'argv': argv})


class LoaderRefusalDiagnosticTests(unittest.TestCase):
    def values(self):
        expected = {'exe':'/selected/wine', 'sha256':'a'*64,
                    'argv':['/selected/wine','/selected/bootstrap.exe','--fixture-map','fixture.vmap']}
        actual = {**expected, 'pid':2, 'start_ticks':3, 'uid':1000,
                  'boot_id':'owned-boot', 'state':'S'}
        return expected, {'bootstrap':{'before':copy.deepcopy(actual),'after':copy.deepcopy(actual)}}

    def test_each_exact_loader_mismatch_is_recorded_without_acceptance(self):
        for key, value in (('exe','/other/wine'), ('sha256','b'*64), ('argv',['changed-title'])):
            expected, context = self.values()
            context['bootstrap']['after'][key] = value
            original = owner.Refusal('exact loader refusal')
            verifier = mock.Mock(side_effect=original)
            records = []
            with self.assertRaises(owner.Refusal) as caught:
                owner.verify_with_loader_diagnostic(verifier, context, expected, records)
            self.assertIs(caught.exception, original)
            self.assertEqual(records[0]['mismatched_fields'], [key])
            self.assertEqual(records[0]['bootstrap_after'][key], value)

    def test_refusal_keeps_event_unexecuted(self):
        expected, context = self.values()
        runtime = owner.Owner(Backend(), {}, lambda:None, clock=lambda:0)
        runtime.started = 0
        runtime.armed.set()
        event = mock.Mock()
        records = []
        original = owner.Refusal('runtime refused')
        def checked(actual):
            return owner.verify_with_loader_diagnostic(mock.Mock(side_effect=original), actual, expected, records)
        with self.assertRaises(owner.Refusal) as caught:
            runtime.verify_input(lambda:context, checked, event)
        self.assertIs(caught.exception, original)
        event.assert_not_called()
        self.assertEqual(len(records), 1)

    def test_serialized_record_excludes_payloads_and_bounds_unknown_values(self):
        expected, context = self.values()
        context.update(nonce='secret-nonce', witness=b'secret-witness', environment={'HOME':'secret-home'})
        context['bootstrap']['after'].update(secret='secret-value', exe='x'*1025, argv=['x']*17)
        result = owner.loader_refusal_diagnostic(expected, context)
        encoded = json.dumps(result)
        for secret in ('secret-nonce','secret-witness','secret-home','secret-value','x'*1025):
            self.assertNotIn(secret, encoded)
        self.assertIsNone(result['bootstrap_after']['exe'])
        self.assertIsNone(result['bootstrap_after']['argv'])
        self.assertEqual(result['bootstrap_after']['unrecorded_fields'], ['exe','argv'])
        self.assertLess(len(encoded), 40000)
        self.assertEqual(json.loads(encoded), result)

    def test_diagnostic_failure_does_not_replace_original_exception(self):
        original = owner.Refusal('original')
        with mock.patch.object(owner,'loader_refusal_diagnostic',side_effect=ValueError('diagnostic')):
            with self.assertRaises(owner.Refusal) as caught:
                owner.verify_with_loader_diagnostic(mock.Mock(side_effect=original), {}, {}, [])
        self.assertIs(caught.exception, original)

    def test_environment_diagnostic_is_fixed_boolean_only_and_binds_available_game_identity(self):
        expected, context = self.values()
        keys=('WAYLAND_DISPLAY','LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH',
              'PULSE_SERVER','PIPEWIRE_REMOTE','WINELOADER','WINESERVER')
        game=dict(pid=23,start_ticks=100,uid=1000,unknown='secret-game')
        context.update(before=game,after=game,environment={**dict.fromkeys(keys,'secret-route'),
            'UNRELATED':'secret-unrelated','WINELOADER':'secret-selected-wine','WINESERVER':'secret-selected-server'})
        record=owner.loader_refusal_diagnostic(expected,context,selected_providers={
            'wine':'secret-selected-wine','server':'secret-selected-server'})
        self.assertTrue(record['environment_record_available'])
        self.assertEqual(list(record['forbidden_environment_nonempty']),list(keys))
        self.assertEqual(record['forbidden_environment_nonempty'],dict.fromkeys(keys,True))
        self.assertEqual(record['environment_matches_selected_provider'],{'WINELOADER':True,'WINESERVER':True})
        self.assertEqual(record['selected_game_before'],dict(pid=23,start_ticks=100,uid=1000,unrecorded_fields=[]))
        self.assertEqual(record['selected_game_after'],record['selected_game_before'])
        self.assertTrue(record['mismatched_fields_are_differences_not_failed_predicates'])
        encoded=json.dumps(record)
        for secret in ('secret-route','secret-unrelated','secret-selected-wine','secret-selected-server','secret-game'):
            self.assertNotIn(secret,encoded)
        context['environment']={'WINELOADER':'foreign-secret','WINESERVER':''}
        record=owner.loader_refusal_diagnostic(expected,context,selected_providers={
            'wine':'selected-secret','server':'selected-server'})
        self.assertEqual(record['environment_matches_selected_provider'],{'WINELOADER':False,'WINESERVER':False})
        self.assertEqual(record['forbidden_environment_nonempty'],{key:key=='WINELOADER' for key in keys})

    def test_refusal_receipt_preserves_original_exception_and_unavailable_environment(self):
        expected,context=self.values()
        original=owner.Refusal('unchanged refusal')
        records=[]
        with self.assertRaises(owner.Refusal) as caught:
            owner.verify_with_loader_diagnostic(mock.Mock(side_effect=original),context,expected,records,
                selected_providers={'wine':'secret-selected','server':'secret-server'})
        self.assertIs(caught.exception,original)
        self.assertFalse(records[0]['environment_record_available'])
        self.assertTrue(all(value is False for value in records[0]['forbidden_environment_nonempty'].values()))
        self.assertEqual(records[0]['selected_game_before']['unrecorded_fields'],['pid','start_ticks','uid'])

    def test_success_does_not_record_and_refusal_records_are_capped(self):
        expected, context = self.values()
        records = []
        self.assertEqual(owner.verify_with_loader_diagnostic(lambda _:42,context,expected,records),42)
        self.assertEqual(records, [])
        for _ in range(9):
            with self.assertRaises(owner.Refusal):
                owner.verify_with_loader_diagnostic(mock.Mock(side_effect=owner.Refusal('no')),context,expected,records)
        self.assertEqual(len(records),8)


if __name__ == '__main__':
    unittest.main()
