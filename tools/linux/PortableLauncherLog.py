"""Plain-text launcher session log shared by the launcher window and its owned children.

Each line has the local time with milliseconds, the seconds since that process
opened the log and the writing role. Logging never changes launcher behaviour:
every failure only stops logging. The home folder is written as ~ and the user
name in paths as $USER; the log never records tokens, URL queries or the
environment.
"""
# SPDX-License-Identifier: GPL-2.0-or-later

from datetime import datetime
import json
import os
from pathlib import Path
import platform
import re
import sys
import time

LOG_NAME = 'launcher.log'
PREVIOUS_NAME = 'launcher.previous.log'
MAX_BYTES = 1024 * 1024
# Owned children and a handed-over launcher append to the session named here.
ENVIRONMENT = 'NH_LAUNCHER_LOG'


class _Session:
    path = None
    role = 'launcher'
    started = time.monotonic()
    full = False
    report_phases = False


_session = _Session()


def log_directory():
    """The XDG state folder: the managed game profile must not be created before setup."""
    state = os.environ.get('XDG_STATE_HOME', '')
    return (Path(state) if state.startswith('/') else Path.home() / '.local/state') / 'new-horizons'


def _open(path, role):
    _session.path, _session.role, _session.started, _session.full = path, role, time.monotonic(), False


def start_session(role='launcher'):
    """Keep the previous session as launcher.previous.log and start a new log.

    A launcher handed over from an older copy continues the session it names."""
    continued = os.environ.get(ENVIRONMENT, '')
    try:
        directory = log_directory()
        path = directory / LOG_NAME
        if continued == str(path):
            _open(path, role)
            write('Continuing in the launcher of the selected update: ' + str(Path(sys.argv[0]).absolute().parent))
            return path
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        if path.is_file() and not path.is_symlink():
            os.replace(path, directory / PREVIOUS_NAME)
        _open(path, role)
    except OSError:
        _session.path = None
    return _session.path


def join_session(role, report_phases=False):
    """A child appends to the session its launcher named; phases also go to stderr."""
    value = os.environ.get(ENVIRONMENT, '')
    if value.startswith('/') and Path(value).name == LOG_NAME:
        _open(Path(value), role)
    _session.report_phases = report_phases


def child_environment(environment=None):
    result = dict(os.environ if environment is None else environment)
    result.pop(ENVIRONMENT, None)
    if _session.path is not None:
        result[ENVIRONMENT] = str(_session.path)
    return result


def redact(text):
    """One line per entry; the home folder and the user name in paths are replaced."""
    text = ' | '.join(str(text).splitlines())
    home = str(Path.home()).rstrip('/')
    if len(home) > 1:
        text = re.sub(re.escape(home) + r'(?=/|\b|$)', '~', text)
    user = os.environ.get('USER') or os.environ.get('LOGNAME') or ''
    if len(user) >= 3:
        text = re.sub(r'(?<=/)' + re.escape(user) + r'(?![A-Za-z0-9_.-])', '$USER', text)
    return text


def write(message):
    path = _session.path
    if path is None:
        return
    try:
        now = datetime.now().astimezone()
        prefix = (f'{now:%Y-%m-%d %H:%M:%S}.{now.microsecond // 1000:03d} {now:%z} '
                  f'+{time.monotonic() - _session.started:.3f}s {_session.role:<8} ')
        text = redact(message)
        descriptor = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        try:
            if os.fstat(descriptor).st_size >= MAX_BYTES:
                if _session.full:
                    return
                _session.full = True
                text = 'Log size limit reached; later lines are omitted.'
            # One write per line keeps concurrent launcher and child lines whole.
            os.write(descriptor, (prefix + text + '\n').encode('utf-8', 'replace'))
        finally:
            os.close(descriptor)
    except OSError:
        pass


def describe_error(error):
    """Exception type and message with its causes and network reasons."""
    parts = []
    while error is not None and len(parts) < 5:
        text = f'{type(error).__module__}.{type(error).__qualname__}: {error}'.removeprefix('builtins.')
        reason = getattr(error, 'reason', None)
        if isinstance(reason, BaseException):
            text += f' [reason {type(reason).__qualname__}: {reason}]'
        parts.append(text)
        error = error.__cause__ or error.__context__
    return ' <- caused by '.join(parts)


def phase(name):
    """Record an update-check phase; the window names it if its deadline stops the child."""
    write('Phase: ' + name)
    if _session.report_phases:
        print(json.dumps({'phase': name}), file=sys.stderr, flush=True)


def hint():
    """Status-line suffix naming the log so a player can open or send it."""
    return '' if _session.path is None else ' Details: ' + redact(_session.path)


def write_header(version, code):
    write(f'New Horizons launcher {version} (Linux), code {code}, pid {os.getpid()}')
    distribution = 'unknown distribution'
    try:
        distribution = platform.freedesktop_os_release().get('PRETTY_NAME', distribution)
    except (OSError, AttributeError):
        pass
    write(f'OS: {distribution}; {platform.system()} {platform.release()} {platform.machine()}')
    write(f'Python {platform.python_version()}')
