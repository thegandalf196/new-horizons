"""Kill an owned native NHART preparation, then recover its exact disposable cache."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time
import uuid

from prepare_nhart_native_pin import verified_pin
import nhart


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
                           'prefixSHA256': hashlib.sha256(prefix).hexdigest()}
    return None


def kill_owned(process):
    if process.poll() is None:
        process.kill()  # Popen's owned process handle; no PID/name/tree search.
    return process.wait(timeout=10)


def run_check(probe, pack, output, pin, records, *, popen=subprocess.Popen,
              clock=time.monotonic, sleep=time.sleep, witness_timeout=90, recovery_timeout=180):
    plain_ancestors(output.parent)
    output.mkdir(mode=0o700)  # Exclusive fresh root; never reuse a caller's cache.
    cache = output / 'cache'
    cache.mkdir(mode=0o700)
    token = uuid.uuid4().hex[:16]
    child = 'nhart-runtime-' + '-'.join(token[i:i + 4] for i in range(0, 16, 4))
    owned = cache / child
    base = [str(probe), '--pack', str(pack), '--cache-root', str(cache),
            '--expected-pack-sha256', pin[0], '--expected-resources', str(pin[1])]
    report = {'format': 1, 'pass': False, 'platform': sys.platform,
              'sourceRevision': os.environ.get('GITHUB_SHA', ''), 'packSHA256': pin[0],
              'expectedResources': pin[1], 'probeSHA256': file_hash(probe),
              'nativeCoreDLLSHA256': file_hash(probe.parent / 'VCMI_lib.dll'),
              'pollIntervalSeconds': 0.005, 'witnessTimeoutSeconds': witness_timeout,
              'recoveryTimeoutSeconds': recovery_timeout, 'ownedChild': child}
    process = None
    try:
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
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('This acceptance gate requires native Windows')
    probe, pack = args.probe.absolute(), args.pack.absolute()
    plain(probe)
    plain(pack)
    plain(probe.parent / 'VCMI_lib.dll')
    pin = verified_pin(args.repo, pack)
    records = nhart.verify(pack)['entries']
    report = run_check(probe, pack, args.output.absolute(), pin, records)
    print('NHART native interrupted preparation recovery: ' + ('PASS' if report['pass'] else 'FAIL'))
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
