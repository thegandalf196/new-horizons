# SPDX-License-Identifier: GPL-2.0-or-later
"""Pinned scanner transport: bounded I/O, no inherited config or output."""
import hashlib
import ctypes
import errno
import os
from pathlib import Path
import platform
import selectors
import subprocess
import tarfile
import time
import threading

ARCHIVE_SIZE = 8230402
ARCHIVE_SHA = '551f6fc83ea457d62a0d98237cbad105af8d557003051f41f3e7ca7b3f2470eb'
LICENSE_SHA = 'e3884b252b3bfc045e55be43a34d1e80da070bc6f804ac95bf4660e97d62ebc6'
TEMPLATE = '[{{range $i,$f := .}}{{if $i}},{{end}}{"file":{{printf "%q" $f.File}},"rule":{{printf "%q" $f.RuleID}},"start":{{$f.StartLine}},"end":{{$f.EndLine}}}{{end}}]\n'


class ScannerError(ValueError):
    """Only fixed diagnostic codes may leave the scanner transport."""


def network_preexec():
    """Pinned Linux-x86_64 child: deny network/socket and io_uring creation in-kernel."""
    if platform.system() != 'Linux' or platform.machine() != 'x86_64' or threading.active_count() != 1:
        raise ScannerError('scanner-network-sandbox')
    if len(tuple(Path('/proc/self/task').iterdir())) != 1:
        raise ScannerError('scanner-network-sandbox')
    class Filter(ctypes.Structure):
        _fields_ = [('code', ctypes.c_ushort), ('jt', ctypes.c_ubyte),
                    ('jf', ctypes.c_ubyte), ('k', ctypes.c_uint)]
    class Program(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ushort), ('filters', ctypes.POINTER(Filter))]
    denied = (41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 288, 299, 307, 425, 426, 427)
    rows = [(0x20, 0, 0, 4), (0x15, 1, 0, 0xc000003e), (0x06, 0, 0, 0x80000000),
            (0x20, 0, 0, 0), (0x45, 0, 1, 0x40000000), (0x06, 0, 0, 0x00050000 | errno.EPERM)]
    for number in denied:
        rows += [(0x15, 0, 1, number), (0x06, 0, 0, 0x00050000 | errno.EPERM)]
    rows.append((0x06, 0, 0, 0x7fff0000))
    filters = (Filter * len(rows))(*(Filter(*row) for row in rows))
    program = Program(len(rows), filters)
    libc = ctypes.CDLL(None, use_errno=True)
    def install():
        # No imports or Python locks after fork. Filter persists through exec/descendants.
        if libc.prctl(38, 1, 0, 0, 0) or libc.prctl(22, 2, ctypes.byref(program), 0, 0):
            raise OSError('network-sandbox-install')
    return install


def check_rss(child):
    """Owned direct-child abort sample, not a kernel hard-memory ceiling."""
    if child.poll() is not None:
        return
    try:
        with open(f'/proc/{child.pid}/status', 'rb') as source:
            status = source.read(16384)
    except FileNotFoundError:
        if child.poll() is not None:
            return
        raise ScannerError('scanner-resource-status') from None
    import re
    match = re.search(rb'^VmRSS:\s+(\d+) kB$', status, re.M)
    if match and int(match[1]) > 512 * 1024:
        raise ScannerError('scanner-rss-abort')


def cleanup(child):
    try:
        if child.poll() is None:
            try:
                child.kill()
            except ProcessLookupError:
                pass
        child.wait(timeout=2)
    except (OSError, subprocess.TimeoutExpired):
        raise ScannerError('scanner-cleanup-incomplete') from None
    finally:
        for stream in (child.stdin, child.stdout, child.stderr):
            stream.close()


def run(argv, data, cwd, deadline, *, stdout_limit=1048576, stderr_limit=65536,
        environment=None, timeout=12):
    """One child, bounded concurrent pipes and wall clock; never expose stderr."""
    env = {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C', 'GOMAXPROCS': '1', 'GOMEMLIMIT': '256MiB',
           'HOME': str(cwd), 'XDG_CONFIG_HOME': str(cwd)}
    if environment is not None:
        env = environment
    stop = min(deadline, time.monotonic() + timeout)
    if time.monotonic() >= stop:
        raise ScannerError('scanner-deadline')
    output = {1: bytearray(), 2: bytearray()}
    child = None
    try:
        child = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, cwd=cwd, env=env,
                                 preexec_fn=network_preexec() if environment is None else None)
        with selectors.DefaultSelector() as selector:
            for stream, events, key in ((child.stdin, selectors.EVENT_WRITE, 0),
                                        (child.stdout, selectors.EVENT_READ, 1),
                                        (child.stderr, selectors.EVENT_READ, 2)):
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, events, key)
            offset = 0
            while selector.get_map():
                if environment is None:
                    check_rss(child)
                remaining = stop - time.monotonic()
                if remaining <= 0:
                    raise ScannerError('scanner-deadline')
                for event, _ in selector.select(min(remaining, 0.1)):
                    stream, key = event.fileobj, event.data
                    if key == 0:
                        if offset == len(data):
                            selector.unregister(stream)
                            stream.close()
                            continue
                        try:
                            offset += os.write(stream.fileno(), data[offset:offset + 65536])
                        except BrokenPipeError:
                            selector.unregister(stream)
                            stream.close()
                    else:
                        chunk = os.read(stream.fileno(), 65536)
                        if not chunk:
                            selector.unregister(stream)
                            stream.close()
                            continue
                        limit = stdout_limit if key == 1 else stderr_limit
                        if len(output[key]) + len(chunk) > limit:
                            raise ScannerError('scanner-output-limit')
                        output[key].extend(chunk)
            while child.poll() is None:
                if environment is None:
                    check_rss(child)
                remaining = stop - time.monotonic()
                if remaining <= 0:
                    raise ScannerError('scanner-deadline')
                try:
                    child.wait(timeout=min(remaining, 0.1))
                except subprocess.TimeoutExpired:
                    pass
            code = child.returncode
            if offset != len(data):
                raise ScannerError('scanner-input-incomplete')
            return code, bytes(output[1]), bytes(output[2])
    except (OSError, subprocess.SubprocessError):
        raise ScannerError('scanner-process-unavailable') from None
    finally:
        if child is not None:
            cleanup(child)


def prepare(archive, owned):
    """Authenticate before tar parsing; copy only fixed regular members."""
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ScannerError('unsupported-scanner-platform')
    try:
        archive = Path(archive)
        if archive.is_symlink() or not archive.is_file():
            raise ScannerError('scanner-archive-unavailable')
        with archive.open('rb') as source:
            payload = source.read(ARCHIVE_SIZE + 1)
        if len(payload) != ARCHIVE_SIZE or hashlib.sha256(payload).hexdigest() != ARCHIVE_SHA:
            raise ScannerError('scanner-archive-identity')
        # Parse the authenticated bytes, not a pathname that could change afterward.
        import io
        with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as tar:
            members = tar.getmembers()
            expected = {'LICENSE': 1069, 'README.md': 29998, 'gitleaks': 21958840}
            if (len(members) != len(expected) or
                    {m.name: m.size for m in members} != expected or
                    any(not m.isfile() for m in members)):
                raise ScannerError('scanner-archive-members')
            for name in ('gitleaks', 'LICENSE'):
                data = tar.extractfile(name).read(expected[name] + 1)
                if len(data) != expected[name]:
                    raise ScannerError('scanner-archive-member-size')
                if name == 'LICENSE' and hashlib.sha256(data).hexdigest() != LICENSE_SHA:
                    raise ScannerError('scanner-license-identity')
                target = Path(owned) / name
                with target.open('xb') as destination:
                    destination.write(data)
                target.chmod(0o700 if name == 'gitleaks' else 0o600)
        template = Path(owned) / 'metadata-template'
        template.write_text(TEMPLATE, encoding='utf8')
        template.chmod(0o600)
        return Path(owned) / 'gitleaks', template
    except (OSError, tarfile.TarError, KeyError, EOFError):
        raise ScannerError('scanner-archive-unavailable') from None


def command(binary, template, owned, payload, *, witness=False):
    return [str(binary), 'dir', str(payload), '--no-banner', '--no-color', '--redact=100',
            '--ignore-gitleaks-allow', '--gitleaks-ignore-path', str(owned),
            '--log-level=info', '--report-format=template', '--report-template', str(template),
            '--report-path=-', '--exit-code=42', '--timeout=10' if witness else '--timeout=240', '--max-decode-depth=0',
            '--max-archive-depth=0', '--max-target-megabytes=0']
