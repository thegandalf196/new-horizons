#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Finite private Wine owner. Called ONLY by the maintained admission helper.

No process-name scans, shell launch, host environment inheritance or input driver.
The owner is a subreaper before any Wine fork. One input request is ONE logical
click/move/key gesture, not an XTest press/release packet. Fresh game proof is
required before/after that gesture; the exact maintained input CLI separately
verifies its owned Xvfb. No reusable input lease or per-XTest-packet proof claim.
Caller owns display/capture cleanup and the independent640s outer deadline.
"""
import ctypes
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import secrets
import select
import signal
import stat
import subprocess
import sys
import threading
import time

INPUT = 600
TERM = 605
KILL = 610
DISPLAY = 630
FINAL = 640
STOP_EXEC = "import os,signal,sys;os.kill(os.getpid(),signal.SIGSTOP);os.execv(sys.argv[1],sys.argv[1:])"


class Refusal(RuntimeError):
    pass


def input_argv(request):
    if not isinstance(request, dict) or type(request.get('id')) is not int or request['id'] < 0:
        raise Refusal('Input request requires a nonnegative integer id')
    action = request.get('action')
    if action in ('move', 'click') and set(request) == {'id', 'action', 'x', 'y'}:
        if type(request['x']) is int and type(request['y']) is int and 0 <= request['x'] < 1280 and 0 <= request['y'] < 800:
            return [action, str(request['x']), str(request['y'])]
    if action == 'key' and set(request) == {'id', 'action', 'key'}:
        if isinstance(request['key'], str) and request['key'] in {'Escape', 'Return', 'f', 'F', 'e', 'E', 'space', 'Left', 'Right', 'Up', 'Down'}:
            return ['key', request['key']]
    raise Refusal('Only one bounded click/move/allowlisted key is permitted')


def file_proof(path):
    """Hash and stat the SAME open regular file, refusing change while reading."""
    path = Path(path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode):
            raise Refusal("Artifact is not regular")
        digest = hashlib.sha256()
        while block := os.read(fd, 1024 * 1024):
            digest.update(block)
        after = os.fstat(fd)
        keys = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        if any(getattr(before, key) != getattr(after, key) for key in keys):
            raise Refusal("Artifact changed around hash")
        return {"path": str(path), "device": f"{os.major(before.st_dev):02x}:{os.minor(before.st_dev):02x}",
                "inode": before.st_ino, "sha256": digest.hexdigest()}
    finally:
        os.close(fd)


class Linux:
    def __init__(self):
        self.pid = os.getpid()
        self.uid = os.getuid()
        self.boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()

    def subreaper(self):
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
            raise Refusal("Cannot enable Wine child subreaper")
        value = ctypes.c_int()
        if libc.prctl(37, ctypes.byref(value), 0, 0, 0) != 0 or value.value != 1:
            raise Refusal("Wine child subreaper not armed")

    def kernel_snapshot(self, pid):
        """Cleanup identity does not require dumpable exe/environ visibility."""
        proc = Path('/proc') / str(pid)
        before = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
        uid = proc.stat().st_uid
        after = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
        if before[19] != after[19] or before[1] != after[1]:
            raise Refusal('Process changed during kernel identity read')
        return {'pid': pid, 'start_ticks': int(after[19]), 'uid': uid,
                'boot_id': self.boot, 'state': after[0], 'ppid': int(after[1])}

    def snapshot(self, pid):
        proc = Path('/proc') / str(pid)
        fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
        if fields[0] == 'Z':
            return {"pid": pid, "start_ticks": int(fields[19]), "uid": proc.stat().st_uid,
                    "boot_id": self.boot, "state": 'Z', "exe": '', "sha256": '', "argv": []}
        exe = (proc / 'exe').resolve(strict=True)
        proof = file_proof(exe)
        current = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
        if fields[19] != current[19] or fields[1] != current[1]:
            raise Refusal("Process changed during identity read")
        return {"pid": pid, "start_ticks": int(current[19]), "uid": proc.stat().st_uid,
                "boot_id": self.boot, "state": current[0], "exe": str(exe),
                "sha256": proof['sha256'], "argv": (proc / 'cmdline').read_bytes().decode().strip('\0').split('\0')}

    def environment(self, pid):
        return dict(row.split('=', 1) for row in (Path('/proc') / str(pid) / 'environ').read_bytes().decode().split('\0') if '=' in row)

    def children(self, pid):
        # Kernel ancestry only, including reparented services on this subreaper.
        children = set()
        for task in (Path('/proc') / str(pid) / 'task').iterdir():
            try:
                children.update(map(int, (task / 'children').read_text().split()))
            except FileNotFoundError:
                pass
        return children

    def pidfd(self, pid):
        return os.pidfd_open(pid)

    def exited(self, fd):
        poll = select.poll()
        poll.register(fd, select.POLLIN | select.POLLHUP)
        events = poll.poll(0)
        if any(flags & (select.POLLERR | select.POLLNVAL) for _, flags in events):
            raise Refusal('Retained pidfd readiness is invalid')
        return any(flags & (select.POLLIN | select.POLLHUP) for _, flags in events)

    def send(self, fd, sig):
        signal.pidfd_send_signal(fd, sig)

    def close(self, fd):
        os.close(fd)

    def reap(self):
        result = []
        while True:
            try:
                pid, status = os.waitpid(-1, os.WNOHANG)
            except ChildProcessError:
                break
            if not pid:
                break
            result.append((pid, os.waitstatus_to_exitcode(status)))
        return result

    def mappings(self, pid, expected):
        rows = []
        selected_names = {Path(path).name.casefold() for path in expected}
        for line in (Path('/proc') / str(pid) / 'maps').read_text().splitlines():
            columns = line.split(None, 5)
            if len(columns) != 6:
                continue
            path = columns[5]
            for old, new in ((r'\040', ' '), (r'\011', '\t'), (r'\012', '\n'), (r'\134', '\\')):
                path = path.replace(old, new)
            if path in expected or Path(path).name.casefold() in selected_names:
                proof = file_proof(path)
                if proof['device'] != columns[3] or proof['inode'] != int(columns[4]):
                    raise Refusal("Mapped inode differs from hashed open file")
                rows.append(proof)
        return rows


class Owner:
    """Small ownership state machine; injected backend makes tests process-free."""
    def __init__(self, backend, environment, verify_xvfb, *, clock=time.monotonic):
        self.backend, self.environment, self.verify_xvfb = backend, environment, verify_xvfb
        self.clock = clock
        self.started = None
        self.ledger = {}
        self.reaped = {}
        self.parent_reaped = {}
        self.processes = {}
        self.history = []
        self.errors = []
        self.lock = threading.RLock()
        self.armed = threading.Event()
        self.finished = threading.Event()
        # Immutable tuples published by the main thread. Watchdog never takes
        # the proof lock or performs proc/hash/adoption I/O. Retained pidfds
        # identify process lifetimes even after exec or numeric PID reuse.
        self.signal_fds = ()

    def publish_fds(self):
        self.signal_fds = tuple(row['pidfd'] for pid, row in self.ledger.items() if not self.completed(pid))

    def completed(self, pid):
        return pid in self.reaped or pid in self.parent_reaped

    def graph_drained(self):
        return all(self.completed(pid) for pid in self.ledger)

    def retain(self, pid, entry):
        """Publish a newly retained lifetime, never inherit numeric-PID status."""
        if pid in self.ledger:
            if not self.completed(pid):
                self.backend.close(entry['pidfd'])
                raise Refusal('New lifetime conflicts with unreaped owned PID')
            self.history.append({'pid': pid, 'entry': self.ledger[pid], 'status': self.reaped.get(pid),
                                 'parent_reaped': self.parent_reaped.get(pid)})
            self.reaped.pop(pid, None)
            self.parent_reaped.pop(pid, None)
            self.processes.pop(pid, None)
        self.ledger[pid] = entry
        self.publish_fds()

    def deadline_signal(self, sig):
        for fd in self.signal_fds:
            try:
                self.backend.send(fd, sig)
            except ProcessLookupError:
                pass
            except Exception as error:
                self.errors.append('Deadline pidfd signal failed: ' + str(error))

    def age(self):
        if self.started is None:
            raise Refusal("Owner not armed")
        return self.clock() - self.started

    def adopt(self):
        with self.lock:
            queue, seen = [self.backend.pid], set()
            while queue:
                parent = queue.pop()
                try:
                    parent_before = self.backend.kernel_snapshot(parent)
                    if parent_before['uid'] != self.backend.uid or parent_before['boot_id'] != self.backend.boot:
                        if parent == self.backend.pid:
                            raise Refusal('Foreign owner identity')
                        self.errors.append('Foreign retained parent identity')
                        continue
                    if parent != self.backend.pid:
                        retained = self.ledger[parent]['identity']
                        if retained is None:
                            # Direct child's pidfd still permits cleanup, but
                            # missing parent proof never permits descendant adoption.
                            continue
                        if any(parent_before[k] != retained[k] for k in ('pid', 'start_ticks', 'uid', 'boot_id')):
                            self.errors.append('Retained parent lifetime changed')
                            continue
                    children = self.backend.children(parent)
                    parent_after = self.backend.kernel_snapshot(parent)
                    if any(parent_before[k] != parent_after[k] for k in ('pid', 'start_ticks', 'uid', 'boot_id')):
                        raise Refusal('Parent changed around child enumeration')
                except FileNotFoundError:
                    continue
                for pid in children - seen:
                    seen.add(pid); queue.append(pid)
                    if pid in self.ledger and not self.completed(pid):
                        continue
                    try:
                        identity = self.backend.kernel_snapshot(pid)
                        if identity['uid'] != self.backend.uid or identity['boot_id'] != self.backend.boot:
                            raise Refusal("Foreign descendant identity")
                        if identity['ppid'] not in (parent, self.backend.pid):
                            raise Refusal('Descendant outside retained lineage')
                        fd = self.backend.pidfd(pid)
                        try:
                            after = self.backend.kernel_snapshot(pid)
                            if any(identity[k] != after[k] for k in ('pid', 'start_ticks', 'uid', 'boot_id')) or after['ppid'] not in (parent, self.backend.pid):
                                raise Refusal("Descendant reused or changed lineage around pidfd")
                            if after['ppid'] != self.backend.pid:
                                current_parent = self.backend.kernel_snapshot(parent)
                                if any(parent_before[k] != current_parent[k] for k in ('pid', 'start_ticks', 'uid', 'boot_id')):
                                    raise Refusal('Parent changed around descendant retention')
                        except Exception:
                            self.backend.close(fd)
                            raise
                        # Kernel ancestry+subreaper+pidfd establish cleanup
                        # ownership even if Wine cleared environ. INPUT remains
                        # fail-closed on actual environ in verify_runtime.
                        entry = {'identity': identity, 'pidfd': fd,
                                 'initial_environment_empty': None}
                        self.retain(pid, entry)
                        try:
                            actual = self.backend.environment(pid)
                            entry['initial_environment_empty'] = not bool(actual)
                            entry['environment_diagnostic'] = {'status': 'read'}
                        except (FileNotFoundError, PermissionError) as error:
                            entry['environment_diagnostic'] = {'status': 'unreadable',
                                'error_type': type(error).__name__, 'errno': error.errno}
                    except FileNotFoundError:
                        continue
            return seen

    def signal_owned(self, sig):
        with self.lock:
            self.adopt()
            for pid, entry in self.ledger.items():
                if self.completed(pid):
                    continue
                try:
                    before = self.backend.kernel_snapshot(pid)
                    original = entry['identity']
                    if original is None:
                        # Direct stopped child retained before proof I/O failed.
                        # pidfd alone keeps exact cleanup lifetime ownership.
                        self.backend.send(entry['pidfd'], sig)
                        continue
                    if any(before[k] != original[k] for k in ('pid', 'start_ticks', 'uid', 'boot_id')):
                        raise Refusal("Owned PID identity changed; no signal")
                    if before['state'] == 'Z':
                        continue
                    after = self.backend.kernel_snapshot(pid)
                    if any(before[k] != after[k] for k in ('pid', 'start_ticks', 'uid', 'boot_id', 'ppid')):
                        raise Refusal("Owned kernel lifetime changed around signal")
                    self.backend.send(entry['pidfd'], sig)
                except (FileNotFoundError, ProcessLookupError):
                    pass
                except Exception as error:
                    self.errors.append(str(error))

    def reap(self):
        with self.lock:
            for pid, status in self.backend.reap():
                # Kernel reports only actual children. Unknown adopted children
                # remain failures, never silently treated as certified services.
                if pid not in self.ledger:
                    self.errors.append('Unrecorded adopted child reaped')
                self.reaped[pid] = status
                if pid in self.processes:
                    self.processes[pid].returncode = status
            for pid, entry in self.ledger.items():
                # A Win32 parent may legitimately wait/reap its own child.
                # Direct Popen children always require our actual waitpid status.
                if self.completed(pid) or pid in self.processes or entry['identity'] is None:
                    continue
                if not self.backend.exited(entry['pidfd']):
                    continue
                try:
                    current = self.backend.kernel_snapshot(pid)
                except FileNotFoundError:
                    reason = 'absent'
                except PermissionError:
                    continue  # Unreadable is not absence or permission to forget it.
                else:
                    if all(current[k] == entry['identity'][k] for k in ('pid', 'start_ticks', 'uid', 'boot_id')):
                        continue  # Includes an unreaped zombie of the same lifetime.
                    reason = 'numeric-pid-reused'
                if self.backend.exited(entry['pidfd']):
                    self.parent_reaped[pid] = {'identity': copy.deepcopy(entry['identity']),
                                               'proof': 'retained-pidfd-exited-and-' + reason}
            self.publish_fds()

    def spawn(self, command, *, cwd, stdout, stderr=subprocess.STDOUT):
        """Every direct child stops before exec; record it before permitting work."""
        process = subprocess.Popen([sys.executable, '-c', STOP_EXEC, *command],
                                   env=self.environment, cwd=cwd, stdout=stdout, stderr=stderr)
        # This is our direct Popen child; retain lifetime before potentially
        # blocking identity reads, while the child is still stopped pre-exec.
        fd = self.backend.pidfd(process.pid)
        self.retain(process.pid, {'identity': None, 'pidfd': fd,
                                 'initial_environment_empty': False})
        self.processes[process.pid] = process
        stop_limit = min(self.age() + 5, TERM)
        while self.age() < stop_limit:
            try:
                stopped = self.backend.snapshot(process.pid)
                if stopped['state'] == 'T':
                    break
            except FileNotFoundError:
                pass
            time.sleep(.02)
        else:
            raise Refusal('No owned stopped child')
        actual = self.backend.environment(process.pid)
        interpreter = Path(sys.executable).resolve(strict=True)
        if stopped['exe'] != str(interpreter) or stopped['sha256'] != file_proof(interpreter)['sha256'] or actual != self.environment:
            raise Refusal('Stopped Python identity/environment mismatch')
        self.ledger[process.pid]['identity'] = copy.deepcopy(stopped)
        self.adopt()
        self.backend.send(self.ledger[process.pid]['pidfd'], signal.SIGCONT)
        return process, stopped

    def cleanup(self):
        """Single reap authority, early local grace never waits until minute610."""
        self.signal_owned(signal.SIGTERM)
        grace = min(self.age() + 5, KILL)
        while self.age() < grace:
            self.adopt(); self.reap()
            if self.graph_drained():
                return
            time.sleep(.05)
        self.signal_owned(signal.SIGKILL)
        reap_limit = min(self.age() + 5, DISPLAY)
        while self.age() < reap_limit:
            self.adopt(); self.reap()
            if self.graph_drained():
                return
            time.sleep(.05)
        self.errors.append('Owned Wine descendants not reaped within local/global boundary')

    def verify_input(self, context_factory, verifier, event=None):
        """One fresh proof per individual trusted input callback; no reusable ACK."""
        if not self.armed.is_set() or self.age() >= INPUT or self.finished.is_set():
            raise Refusal("Input outside fixed owner boundary")
        # NEVER hold the ledger lock during witness/maps/callback I/O: watchdog
        # must keep working even when a request's proof or driver blocks.
        self.adopt()
        self.verify_xvfb()
        context = context_factory()
        context['age_after_io'] = self.age()
        context['supervisor_ready_before_resume'] = self.armed.is_set()
        context['owned_service_graph_verified'] = not self.errors
        result = verifier(context)
        if self.age() >= INPUT or self.finished.is_set():
            raise Refusal("Input proof crossed fixed deadline")
        if event is not None:
            event()  # Trusted owned-Xvfb callback only, never a command string.
        if self.age() >= INPUT or self.finished.is_set():
            raise Refusal('Input completion crossed fixed deadline')
        return result

    def watchdog(self):
        self.armed.set()
        while not self.finished.wait(.05):
            # No shared lock, metadata, hashes or proof I/O on this path.
            # Repeated delivery also covers descendants adopted after605/610.
            age = self.age()
            if age >= KILL:
                self.deadline_signal(signal.SIGKILL)
            elif age >= TERM:
                self.deadline_signal(signal.SIGTERM)
            if age >= DISPLAY:
                self.errors.append('Owned Wine cleanup deadline630 exceeded; caller outer640 remains required')
                break
        self.finished.set()


def finish_owner(owner, supervisor):
    """Cleanup failure must never skip timer shutdown or the caller's receipt."""
    try:
        owner.cleanup()
    except Exception as error:
        owner.errors.append('Cleanup exception: ' + str(error))
        # Already retained pidfds remain independently safe despite failed
        # metadata/adoption I/O. Do not abandon their lifetime protection.
        owner.deadline_signal(signal.SIGKILL)
        until = min(owner.age() + 5, DISPLAY)
        while owner.age() < until:
            try:
                owner.reap()
            except Exception as reap_error:
                owner.errors.append('Fallback reap exception: ' + str(reap_error))
                break
            if owner.graph_drained():
                break
            time.sleep(.05)
        if not owner.graph_drained():
            owner.errors.append('Fallback cleanup did not reap every retained lifetime')
    finally:
        owner.finished.set()
        try:
            supervisor.join(timeout=1)
            alive = supervisor.is_alive()
        except Exception as error:
            owner.errors.append('Watchdog shutdown exception: ' + str(error))
            alive = True
        if alive:
            owner.errors.append('Watchdog did not stop; pidfds retained, caller outer640 required')
        else:
            entries = list(owner.ledger.values()) + [row['entry'] for row in owner.history]
            for entry in entries:
                try:
                    owner.backend.close(entry['pidfd'])
                except Exception as error:
                    owner.errors.append('pidfd close failed: ' + str(error))


def content_module():
    """Only the maintained adjacent module; never PYTHONPATH resolution."""
    path = Path(__file__).with_name('nh_wine_profile.py')
    if path.is_symlink() or not path.is_file():
        raise Refusal('Maintained adjacent content module missing or unsafe')
    spec = importlib.util.spec_from_file_location('_nh_wine_profile_owned', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_fullgame(plan, *, verify_xvfb, verify_runtime):
    """Admission/content verification outside the timed Wine ownership window."""
    if not isinstance(plan.get('content'), dict):
        raise Refusal('Pinned private content manifest missing; no Wine fork')
    profile = content_module()
    state = {'staged': False}
    failure = None
    status = None
    final_errors = []
    initial_originals_verified = False
    final_originals_verified = False
    try:
        profile.verify_original_content(plan['content'])
        initial_originals_verified = True
        status = run_staged_fullgame(plan, profile, state, verify_xvfb=verify_xvfb, verify_runtime=verify_runtime)
    except Exception as error:
        failure = error
    finally:
        # Even an admission/runtime refusal must retain an original-byte audit.
        # No huge source hashes are included in input proofs or timing samples.
        if state['staged']:
            try:
                profile.verify_staged_content(plan['content'], Path(plan['root']), verify_copy_bytes=False)
            except Exception as error:
                final_errors.append('Final immutable map/link verification failed: ' + str(error))
        try:
            profile.verify_original_content(plan['content'])
            final_originals_verified = True
        except Exception as error:
            final_errors.append('Final original-content verification failed: ' + str(error))
        (Path(plan['root']) / 'content-final.json').write_text(json.dumps({
            'staged': state['staged'], 'final_errors': final_errors,
            'runtime_failure': str(failure) if failure is not None else None,
            'initial_originals_verified': initial_originals_verified,
            'final_originals_verified': final_originals_verified,
            'originals_unchanged': initial_originals_verified and final_originals_verified
        }, indent=2))
    if final_errors:
        raise Refusal('Final content verification failed; retained private receipt') from failure
    if failure is not None:
        raise failure
    return status


def render_timing_environment(plan):
    """Only the explicitly admitted diagnostic keys, never ambient inheritance."""
    contract = plan.get('render_timings', {'enabled': False, 'limit': None})
    if (not isinstance(contract, dict) or set(contract) != {'enabled', 'limit'}
            or type(contract['enabled']) is not bool):
        raise Refusal('Invalid render timing contract')
    if not contract['enabled']:
        if contract['limit'] is not None:
            raise Refusal('Disabled render timing contract has a limit')
        return {}
    if type(contract['limit']) is not int or not 1 <= contract['limit'] <= 16384:
        raise Refusal('Invalid admitted render timing limit')
    return {'NH_RENDER_LOAD_TIMINGS': '1', 'NH_RENDER_LOAD_TIMING_LIMIT': str(contract['limit'])}


def owned_game_candidates(owner, bootstrap_pid, expected):
    """Exact mapped package among retained kernel descendants, not PID/name discovery."""
    matches = []
    for pid, entry in list(owner.ledger.items()):
        if pid == bootstrap_pid or owner.completed(pid) or entry['identity'] is None:
            continue
        try:
            before = owner.backend.snapshot(pid)
            rows = owner.backend.mappings(pid, {row['path'] for row in expected})
            after = owner.backend.snapshot(pid)
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
        keys = ('pid', 'start_ticks', 'uid', 'boot_id')
        if any(before[k] != after[k] or after[k] != entry['identity'][k] for k in keys):
            raise Refusal('Game candidate changed around mapped-file proof')
        pinned = {row['path']: row for row in expected}
        if {row['path'] for row in rows} == set(pinned) and all(row == pinned[row['path']] for row in rows):
            matches.append(after)
    if len(matches) > 1:
        raise Refusal('Multiple owned selected game processes')
    return matches


def run_staged_fullgame(plan, profile, content_state, *, verify_xvfb, verify_runtime):
    """Maintained helper entry. Missing witness-capable artifact refuses pre-fork."""
    executable = Path(plan['executable'])
    image = executable.read_bytes()
    if 'NH_WINE_PRE_SDL_V1'.encode('utf-16-le') not in image or 'NH_PRIVATE_WINE_NONCE'.encode('utf-16-le') not in image:
        raise Refusal('Selected PE lacks compiled private startup witness; no Wine fork')
    if any(key in plan for key in ('launch_nonce', 'stopped_identity', 'loader_identity', 'game_identity', 'bootstrap_mapping')):
        raise Refusal('Launch binding already populated; no second launch')
    timing_env = render_timing_environment(plan)
    input_script = Path(plan['input_program'])
    if input_script != Path(__file__).with_name('nh-private-input.py').resolve(strict=True):
        raise Refusal('Input provider is not exact adjacent maintained program')
    input_pin = plan['input_sha256']
    if file_proof(input_script)['sha256'] != input_pin:
        raise Refusal('Input provider differs from admitted pin')
    artifacts = [executable, executable.parent / 'VCMI_lib.dll'] + [executable.parent / n for n in ('SDL3.dll', 'SDL3_image.dll', 'SDL3_mixer.dll', 'SDL3_ttf.dll')]
    expected = [file_proof(p) for p in artifacts]
    for row in expected:
        digest = plan['executable_sha256'] if row['path'] == str(executable) else plan['dlls'][Path(row['path']).name]
        if row['sha256'] != digest:
            raise Refusal('Selected artifact changed before Wine fork')
    for key in ('wine', 'server'):
        if file_proof(plan[key])['sha256'] != plan[key + '_sha256']:
            raise Refusal('Pinned Wine provider changed')
    bootstrap = Path(plan['bootstrap']) if plan.get('bootstrap') else None
    bootstrap_mapping = file_proof(bootstrap) if bootstrap else None
    if bootstrap and bootstrap_mapping['sha256'] != plan.get('bootstrap_sha256'):
        raise Refusal('Pinned bootstrap changed before Wine fork')
    root = Path(plan['root'])
    for name in ('home', 'prefix', 'localappdata', 'config', 'data', 'cache', 'state', 'runtime'):
        (root / name).mkdir(mode=0o700)
    for name in ('HeroesIII-NewHorizons', 'new-horizons'):
        (root / 'localappdata' / name).mkdir(mode=0o700)
    for name in ('content', 'config', 'cache', 'logs'):
        (root / 'localappdata/HeroesIII-NewHorizons' / name).mkdir(mode=0o700, parents=True)
    (root / 'localappdata/new-horizons/Saves').mkdir(mode=0o700, parents=True)
    profile.stage_content(plan['content'], root)
    content_state['staged'] = True
    profile.verify_staged_content(plan['content'], root, verify_copy_bytes=True)
    gate = root / 'localappdata/.nh-private-wine-gate'
    gate.mkdir(mode=0o700)
    windows = lambda path: 'Z:' + str(path).replace('/', '\\')
    nonce = secrets.token_hex(32)
    env = {'PATH': '/usr/bin:/bin', 'HOME': str(root / 'home'), 'WINEHOME': str(root / 'home'),
           'WINEPREFIX': str(root / 'prefix'), 'LOCALAPPDATA': windows(root / 'localappdata'),
           'DISPLAY': ':191', 'SDL_AUDIODRIVER': 'dummy', 'SDL_AUDIO_DRIVER': 'dummy',
           'SDL_VIDEODRIVER': 'windows', 'SDL_VIDEO_DRIVER': 'windows',
           'NH_PRIVATE_WINE_NONCE': nonce, 'NH_PRIVATE_WINE_LOCALAPPDATA': windows(root / 'localappdata'),
           'NH_PRIVATE_WINE_EXECUTABLE': windows(executable), 'NH_PRIVATE_WINE_FACADE': windows(executable.parent / 'VCMI_lib.dll')}
    for name in ('config', 'data', 'cache', 'state'):
        env['XDG_' + name.upper() + '_HOME'] = str(root / name)
    env['XDG_RUNTIME_DIR'] = str(root / 'runtime')
    env.update(timing_env)
    backend = Linux()
    backend.subreaper()  # BEFORE thread/Popen and before any Wine service fork.
    owner = Owner(backend, env, lambda: verify_xvfb(plan['guard']))
    owner.started = time.monotonic()
    supervisor = threading.Thread(target=owner.watchdog, daemon=True)
    supervisor.start()
    if not owner.armed.wait(1):
        raise Refusal('Supervisor not armed before Wine fork')
    command = [str(plan['wine']), str(bootstrap or executable)]
    process = None
    stopped = None
    status = 1
    try:
        owner.verify_xvfb()
        with (root / 'client.log').open('xb') as log:
            process, stopped = owner.spawn(command, cwd=executable.parent, stdout=log)
            plan['launch_nonce'] = nonce
            plan['stopped_identity'] = copy.deepcopy(stopped)
            plan['loader_identity'] = {'exe': str(plan['wine']), 'sha256': plan['wine_sha256'], 'argv': list(command)}
            game_pid = process.pid
            if bootstrap:
                plan['bootstrap_mapping'] = bootstrap_mapping
                game_pid = None
                # Only kernel-owned descendants are candidates, never a global PID/name scan.
                while owner.age() < 30:
                    owner.adopt(); owner.reap()
                    if process.pid in owner.reaped:
                        raise Refusal('Bootstrap exited before game witness')
                    matches = owned_game_candidates(owner, process.pid, expected)
                    if matches and (gate / 'proof.utf16le').exists():
                        plan['game_identity'] = copy.deepcopy(matches[0])
                        game_pid = matches[0]['pid']
                        break
                    time.sleep(.02)
                if game_pid is None:
                    raise Refusal('Owned game witness discovery exceeded30s')
            def context():
                profile.verify_staged_content(plan['content'], root, verify_copy_bytes=False)
                before = backend.snapshot(game_pid)
                mappings = backend.mappings(game_pid, {p['path'] for p in expected})
                fd = os.open(gate / 'proof.utf16le', os.O_RDONLY | os.O_NOFOLLOW)
                try:
                    info = os.fstat(fd)
                    if not stat.S_ISREG(info.st_mode) or info.st_uid != backend.uid or info.st_size > 8192:
                        raise Refusal('Unsafe Win32 witness file')
                    payload = os.read(fd, 8193)
                    after_info = os.fstat(fd)
                    if (info.st_size, info.st_mtime_ns, info.st_ctime_ns) != (after_info.st_size, after_info.st_mtime_ns, after_info.st_ctime_ns):
                        raise Refusal('Witness changed during read')
                finally:
                    os.close(fd)
                actual_env = backend.environment(game_pid)
                after = backend.snapshot(game_pid)
                result = {'before': before, 'after': after, 'stopped': stopped,
                        'loader': copy.deepcopy(plan['loader_identity']),
                        'mappings': mappings, 'expected_mappings': expected,
                        'environment': actual_env, 'required_environment': {key: env[key] for key in (
                            'HOME', 'WINEHOME', 'WINEPREFIX', 'LOCALAPPDATA', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME',
                            'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'XDG_RUNTIME_DIR')},
                        'witness': payload, 'nonce': nonce,
                        'win32_executable': windows(executable), 'win32_facade': windows(executable.parent / 'VCMI_lib.dll'),
                        'win32_localappdata': windows(root / 'localappdata')}
                if bootstrap:
                    first = backend.snapshot(process.pid)
                    rows = backend.mappings(process.pid, {str(bootstrap)})
                    last = backend.snapshot(process.pid)
                    result['bootstrap'] = {'before': first, 'after': last, 'mappings': rows}
                    result['game_identity'] = copy.deepcopy(plan['game_identity'])
                return result
            while not (gate / 'proof.utf16le').exists() and owner.age() < 8:
                time.sleep(.02)
            owner.verify_input(context, verify_runtime)
            # Exclusive nonce ACK only after actual Win32/Linux/mapping/env proof.
            def ack():
                fd = os.open(gate / 'approved.utf16le', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
                try:
                    payload = nonce.encode('utf-16-le')
                    if os.write(fd, payload) != len(payload):
                        raise Refusal('Short exclusive nonce ACK')
                finally:
                    os.close(fd)
            owner.verify_input(context, verify_runtime, ack)
            buffer, stdin_open = b'', True
            while owner.age() < TERM and not owner.finished.is_set():
                owner.adopt(); owner.reap()
                if process.pid in owner.reaped:
                    status = owner.reaped[process.pid]
                    break
                if stdin_open and select.select([sys.stdin.fileno()], [], [], .05)[0]:
                    block = os.read(sys.stdin.fileno(), 4096)
                    if not block:
                        if buffer:
                            raise Refusal('Incomplete input request at EOF')
                        stdin_open = False  # EOF is not permission or a cancellation.
                        continue
                    buffer += block
                    if len(buffer) > 4096:
                        raise Refusal('Input request exceeds4096bytes')
                    if b'\n' not in buffer:
                        continue
                    line, trailing = buffer.split(b'\n', 1)
                    if trailing:
                        raise Refusal('Queued/batched logical gestures refused')
                    buffer = b''
                    request = json.loads(line)
                    args = input_argv(request)
                    # Reserve the bounded five-second provider startup/execute
                    # interval; no late launch may run past600.
                    if owner.age() >= INPUT - 5:
                        raise Refusal('Insufficient input deadline margin')
                    def gesture():
                        if file_proof(input_script)['sha256'] != input_pin:
                            raise Refusal('Pinned maintained input provider changed')
                        child, _ = owner.spawn([sys.executable, str(input_script), '--guard', str(plan['guard']), *args],
                                               cwd=root, stdout=log)
                        until = min(owner.age() + 2, INPUT)
                        while owner.age() < until:
                            owner.reap()
                            if child.pid in owner.reaped:
                                if owner.reaped[child.pid] != 0:
                                    raise Refusal('Maintained input provider failed')
                                return
                            time.sleep(.01)
                        owner.signal_owned(signal.SIGTERM)
                        raise Refusal('Input provider timeout')
                    owner.verify_input(context, verify_runtime, gesture)
                    owner.verify_input(context, verify_runtime)  # Fresh game proof AFTER actual gesture.
                    print(json.dumps({'id': request['id'], 'input_done': True, 'age': owner.age()}), flush=True)
                else:
                    time.sleep(.02)
    except Exception as error:
        owner.errors.append(str(error))
    finally:
        # Only this explicit private prefix. Never wineserver -k without WINEPREFIX.
        try:
            if process is None:
                raise Refusal('No Wine child launched; no prefix cleanup command')
            if file_proof(plan['server'])['sha256'] != plan['server_sha256']:
                raise Refusal('Wine server changed before prefix cleanup')
            if owner.age() >= KILL:
                raise Refusal('No late prefix server launch after610')
            cleanup, _ = owner.spawn([str(plan['server']), '-k'], cwd=root, stdout=subprocess.DEVNULL)
            until = min(owner.age() + 3, KILL)
            while owner.age() < until and cleanup.pid not in owner.reaped:
                owner.reap(); time.sleep(.02)
            if owner.reaped.get(cleanup.pid) != 0:
                raise Refusal('Prefix wineserver cleanup failed or timed out')
        except Exception as error:
            owner.errors.append(str(error))
        finish_owner(owner, supervisor)
        (root / 'owner-receipt.json').write_text(json.dumps({'owner_pid': backend.pid, 'boot_id': backend.boot,
            'age': owner.age(), 'status': status, 'errors': owner.errors, 'reaped': owner.reaped,
            'parent_reaped': owner.parent_reaped, 'owned_graph_drained': owner.graph_drained(),
            'ledger': {str(pid): row['identity'] for pid, row in owner.ledger.items()},
            'prior_lifetimes': [{'pid': row['pid'], 'identity': row['entry']['identity'], 'status': row['status'],
                                'parent_reaped': row.get('parent_reaped')}
                                for row in owner.history]}, indent=2))
    if owner.errors or owner.age() >= FINAL:
        raise Refusal('Wine ownership failed; retained private receipt')
    return status
