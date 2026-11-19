"""Drain and reap launcher-owned children while reporting startup and update progress."""
# SPDX-License-Identifier: GPL-2.0-or-later

from collections import deque
import os
import signal
import subprocess
import threading
import time


def terminate(process):
    try:
        process.terminate()
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        # Each child starts a new session, so this group contains only this job.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


class StartupCancellation:
    """Serialize readiness with cancellation so a ready game keeps running."""
    def __init__(self):
        self.ready = threading.Event()
        self.requested = threading.Event()
        self.lock = threading.Lock()

    def set(self):
        with self.lock:
            if not self.ready.is_set():
                self.requested.set()

    def mark_ready(self):
        with self.lock:
            self.ready.set()
            self.requested.clear()

    def terminate_if_set(self, process):
        with self.lock:
            if self.requested.is_set() and not self.ready.is_set():
                terminate(process)
                return True
            return False


def run_process(command, environment=None, on_stdout=None, on_stderr=None, cancel=None, timeout=None):
    """Always consume both pipes, then reap the exact child before returning."""
    output, errors = deque(maxlen=80), deque(maxlen=80)
    callback_errors = deque(maxlen=1)
    with subprocess.Popen(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          start_new_session=True) as process:
        def drain(stream, retained, callback):
            with stream:
                while True:
                    raw = stream.readline(16385)
                    if not raw:
                        break
                    text = raw.decode('utf-8', errors='replace')
                    retained.append(text)
                    if callback:
                        try:
                            callback(text.rstrip('\r\n'))
                        except Exception as error:
                            callback_errors.append(error)

        readers = [threading.Thread(target=drain, args=(process.stdout, output, on_stdout)),
                   threading.Thread(target=drain, args=(process.stderr, errors, on_stderr))]
        for reader in readers:
            reader.start()
        deadline = time.monotonic() + timeout if timeout is not None else None
        interrupted = False
        try:
            while process.poll() is None:
                if isinstance(cancel, StartupCancellation):
                    interrupted = cancel.terminate_if_set(process)
                elif cancel is not None and cancel.is_set():
                    interrupted = True
                    terminate(process)
                if interrupted:
                    break
                if deadline is not None and time.monotonic() >= deadline:
                    interrupted = True
                    terminate(process)
                    break
                time.sleep(0.05)
        finally:
            if process.poll() is None:
                terminate(process)
            for reader in readers:
                reader.join(timeout=5)
        if any(reader.is_alive() for reader in readers):
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            for reader in readers:
                reader.join(timeout=5)
            raise RuntimeError('A launcher child kept its output pipes open after exit')
        if interrupted:
            raise InterruptedError('Operation cancelled or timed out')
        if callback_errors:
            raise RuntimeError('Could not report launcher progress') from callback_errors[0]
        return subprocess.CompletedProcess(command, process.returncode, ''.join(output), ''.join(errors))
