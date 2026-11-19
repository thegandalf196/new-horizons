"""Kill an owned native NHART preparation, then recover its exact disposable cache."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import stat
import subprocess
import sys
import time
import uuid

from prepare_nhart_native_pin import verified_pin
import nhart


def normal_windows_name(path):
    value = str(path)
    if value.startswith('\\\\?\\UNC\\'):
        return '\\\\' + value[8:]
    return value[4:] if value.startswith('\\\\?\\') else value


def utf16_units(path):
    return len(normal_windows_name(path).encode('utf-16-le', errors='surrogatepass')) // 2


def extended_windows_name(path):
    value = normal_windows_name(path)
    parsed = PureWindowsPath(value)
    if value.startswith('\\\\.\\') or not parsed.is_absolute() or '..' in parsed.parts:
        raise ValueError('Extended filesystem access requires an absolute normalized Windows path')
    return '\\\\?\\UNC\\' + value[2:] if value.startswith('\\\\') else '\\\\?\\' + value


def filesystem_path(path):
    # Python's observer must work without a process/registry long-path opt-in.
    # The native probe deliberately receives the ordinary spelling instead.
    return Path(extended_windows_name(path)) if sys.platform == 'win32' else path


def native_cache_argument(path):
    value = str(path)
    if sys.platform == 'win32':
        parsed = PureWindowsPath(value)
        if value.startswith(('\\\\?\\', '\\\\.\\')) or not parsed.is_absolute() or '..' in parsed.parts:
            raise ValueError('Native cache argument must be an ordinary absolute Windows path')
    return value


def cache_root(output, deep_cache=False):
    root = output / 'cache'
    if deep_cache:
        while utf16_units(root) <= 260:
            root /= 'nhart-depth-0123456789abcdef0123456789abcdef'
    return root


def plain(path, directory=False):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError('Linked or reparse path rejected')
    if not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)):
        raise ValueError('Unexpected filesystem object type')
    return info


def plain_ancestors(path):
    for parent in reversed((path, *path.parents)):
        plain(parent, directory=True)


def file_hash(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def witness_stage(owned, digest, pack, records):
    staging = owned / 'new-horizons-art' / 'v1.staging'
    if not staging.exists():
        return None
    plain_ancestors(staging)
    pattern = re.compile(re.escape(digest) + r'\.stage-[0-9a-f]{4}(?:-[0-9a-f]{4}){3}')
    for stage in staging.iterdir():
        if not pattern.fullmatch(stage.name):
            raise ValueError('Unexpected staging inventory')
        plain(stage, directory=True)
        for payload in stage.iterdir():
            match = re.fullmatch(r'payload-(0|[1-9][0-9]*)', payload.name)
            if not match:
                raise ValueError('Unexpected staged payload')
            info = plain(payload)
            index = int(match[1])
            if index >= len(records):
                raise ValueError('Staged payload outside archive inventory')
            if not info.st_size:
                continue
            row = records[index]
            if info.st_size > row['size']:
                raise ValueError('Staged payload exceeds archive record')
            # Observe actual bytes matching the pinned archive, not just a name.
            with payload.open('rb') as stream:
                prefix = stream.read(min(64, info.st_size))
            with pack.open('rb') as stream:
                stream.seek(row['offset'])
                if not prefix or stream.read(len(prefix)) != prefix:
                    raise ValueError('Staged bytes differ from pinned archive')
            return stage, {'stage': stage.name, 'payload': payload.name,
                           'observedBytes': info.st_size, 'matchedPrefixBytes': len(prefix),
                           'stageUTF16Units': utf16_units(stage),
                           'payloadUTF16Units': utf16_units(payload),
                           'prefixSHA256': hashlib.sha256(prefix).hexdigest()}
    return None


def kill_owned(process):
    if process.poll() is None:
        process.kill()  # Popen's owned process handle; no PID/name/tree search.
    return process.wait(timeout=10)


def run_check(probe, pack, output, pin, records, *, popen=subprocess.Popen,
              clock=time.monotonic, sleep=time.sleep, witness_timeout=90, recovery_timeout=180,
              deep_cache=False):
    cache = cache_root(output, deep_cache)
    cache_argument = native_cache_argument(cache)
    plain_ancestors(output.parent)
    output.mkdir(mode=0o700)  # Exclusive fresh root; never reuse a caller's cache.
    observed_cache = filesystem_path(cache) if deep_cache else cache
    token = uuid.uuid4().hex[:16]
    child = 'nhart-runtime-' + '-'.join(token[i:i + 4] for i in range(0, 16, 4))
    owned = observed_cache / child
    base = [str(probe), '--pack', str(pack), '--cache-root', cache_argument,
            '--expected-pack-sha256', pin[0], '--expected-resources', str(pin[1])]
    report = {'format': 1, 'pass': False, 'platform': sys.platform,
              'sourceRevision': os.environ.get('GITHUB_SHA', ''), 'packSHA256': pin[0],
              'workflowRunID': os.environ.get('GITHUB_RUN_ID', ''),
              'workflowRunAttempt': os.environ.get('GITHUB_RUN_ATTEMPT', ''),
              'expectedResources': pin[1], 'probeSHA256': file_hash(probe),
              'nativeCoreDLLSHA256': file_hash(probe.parent / 'VCMI_lib.dll'),
              'pollIntervalSeconds': 0.005, 'witnessTimeoutSeconds': witness_timeout,
              'recoveryTimeoutSeconds': recovery_timeout, 'ownedChild': child,
              'cacheMode': 'deep' if deep_cache else 'short',
              'cacheRootUTF16Units': utf16_units(cache),
              'ownedCacheRootUTF16Units': utf16_units(owned),
              'nativeCacheNamespace': 'ordinary',
              'observerCacheNamespace': 'extended' if deep_cache and sys.platform == 'win32' else 'native'}
    process = None
    try:
        report['phase'] = 'create-exclusive-private-cache'
        observed_cache.mkdir(mode=0o700, parents=deep_cache)
        plain_ancestors(observed_cache)
        if deep_cache and utf16_units(cache) <= 260:
            raise ValueError('Deep cache root did not exceed 260 UTF-16 units')
        report['phase'] = 'witness-real-staged-payload'
        with (output / 'killed.stdout.log').open('wb') as stdout, (output / 'killed.stderr.log').open('wb') as stderr:
            process = popen([*base, '--create-owned-cache-child', child], stdin=subprocess.DEVNULL,
                            stdout=stdout, stderr=stderr, shell=False)
            report['killedProcessPID'] = process.pid
            deadline = clock() + witness_timeout
            witness = None
            while process.poll() is None and clock() < deadline:
                witness = witness_stage(owned, pin[0], pack, records)
                if witness:
                    break
                sleep(0.005)
            if witness is None or process.poll() is not None:
                raise ValueError('No live owned staging witness before deadline or process exit')
            stage, report['witness'] = witness
            if (owned / 'new-horizons-art' / 'v1' / pin[0]).exists():
                raise ValueError('Cache was already published before interruption')
            report['killedExitCode'] = kill_owned(process)
            process = None
        plain(stage, directory=True)
        if report['killedExitCode'] == 0 or (owned / 'new-horizons-art' / 'v1' / pin[0]).exists():
            raise ValueError('Interruption did not leave an unpublished real stage')
        report['partialStageSurvivedKill'] = True
        report['phase'] = 'native-recovery-and-all-resource-parity'
        with (output / 'recovered.stdout.log').open('wb') as stdout, (output / 'recovered.stderr.log').open('wb') as stderr:
            process = popen([*base, '--recover-owned-cache-child', child], stdin=subprocess.DEVNULL,
                            stdout=stdout, stderr=stderr, shell=False)
            report['recoveryProcessPID'] = process.pid
            report['recoveryExitCode'] = process.wait(timeout=recovery_timeout)
            process = None
        if report['recoveryExitCode'] or 'PASS: SHA256 vectors;' not in (output / 'recovered.stdout.log').read_text(errors='replace'):
            raise ValueError('Native recovery/parity probe failed')
        published = owned / 'new-horizons-art' / 'v1' / pin[0]
        plain_ancestors(published)
        if stage.exists() or list((owned / 'new-horizons-art' / 'v1.staging').iterdir()):
            raise ValueError('Native recovery left an abandoned stage')
        if len(list(published.iterdir())) != pin[1] + 1:
            raise ValueError('Recovered inventory differs from pinned pack')
        report.update({'phase': 'complete', 'pass': True})
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        report['failureType'] = type(error).__name__
        report['failureReason'] = str(error) if isinstance(error, ValueError) else 'Owned native process or filesystem operation failed'
    finally:
        if process is not None:
            try:
                report['cleanupExitCode'] = kill_owned(process)
            except (OSError, subprocess.TimeoutExpired):
                report.update({'pass': False, 'cleanupFailed': True})
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repo', 'probe', 'pack', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--deep-cache', action='store_true',
                        help='Use an ordinary native cache root longer than 260 UTF-16 units; keep --output short')
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('This acceptance gate requires native Windows')
    probe, pack = args.probe.absolute(), args.pack.absolute()
    plain(probe)
    plain(pack)
    plain(probe.parent / 'VCMI_lib.dll')
    pin = verified_pin(args.repo, pack)
    records = nhart.verify(pack)['entries']
    report = run_check(probe, pack, args.output.absolute(), pin, records, deep_cache=args.deep_cache)
    print('NHART native interrupted preparation recovery: ' + ('PASS' if report['pass'] else 'FAIL'))
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
