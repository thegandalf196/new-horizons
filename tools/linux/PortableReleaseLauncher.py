#!/usr/bin/env python3
"""Activate a verified Linux player update before launching the game."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

from PortableReleaseUpdate import (ASSET_NAME, MAX_ARCHIVE_BYTES, PACKAGE_ROOT as INSTALL_NAME, ReleaseRedirects,
                                   latest_release, private_directory, validate_release_metadata,
                                   verify_player_files)
from urllib.request import build_opener
from PortableLauncherProcess import run_process

STORE_NAME = '.nh-player-updates'
VERSION = re.compile(r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)')
DIGEST = re.compile(r'[0-9a-f]{64}')


def plain(path, directory=False):
    """Reject linked path components before following a player update path."""
    for component in reversed(path.parents):
        if not stat.S_ISDIR(component.lstat().st_mode):
            raise OSError(f'Unexpected parent of {path.name}')
    mode = path.lstat().st_mode
    if not (stat.S_ISDIR(mode) if directory else stat.S_ISREG(mode)):
        raise OSError(f'Unexpected file type for {path.name}')
    return path


def small_json(path):
    with plain(path).open('rb') as source:
        value = source.read(8193)
    if len(value) > 8192:
        raise ValueError(f'Invalid size for {path.name}')
    return json.loads(value)


def version(path):
    value = small_json(path / 'config/newHorizonsVersion.json')['version']
    if not isinstance(value, str) or len(value) > 32 or not VERSION.fullmatch(value):
        raise ValueError('Invalid product version')
    return tuple(map(int, value.split('.')))


def selected_package(store, selection):
    if not isinstance(selection, dict) or set(selection) != {'format', 'version', 'sha256'}:
        raise ValueError('Invalid installed update selection')
    digest = selection['sha256']
    if selection['format'] != 1 or not isinstance(digest, str) or not DIGEST.fullmatch(digest):
        raise ValueError('Invalid installed update identity')
    package = plain(store / 'releases' / digest / INSTALL_NAME, directory=True)
    actual = '.'.join(map(str, version(package)))
    if actual != selection['version']:
        raise ValueError('Installed update version differs from its selection')
    verify_player_files(package)
    return package


def atomic_selection(store, selection):
    fd, filename = tempfile.mkstemp(prefix='.activate-', dir=store)
    staging = Path(filename)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(selection, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(staging, store / 'current.json')
    finally:
        staging.unlink(missing_ok=True)


def prepare_update(original, store, request, current, progress=None, cancel=None):
    wanted = small_json(request)
    if not isinstance(wanted, dict) or not isinstance(wanted.get('version'), str):
        raise ValueError('Invalid update request')
    release = wanted['version']
    if len(release) > 32 or not VERSION.fullmatch(release):
        raise ValueError('Invalid requested version')
    if tuple(map(int, release.split('.'))) <= version(current):
        return current
    worker = plain(original / 'PortableReleaseUpdate.py')
    prepared = run_worker(worker, store, request, progress=progress, cancel=cancel)
    selection = {'format': 1, 'version': prepared['version'], 'sha256': prepared['sha256']}
    if selection['version'] != release or selection['sha256'] != wanted.get('sha256'):
        raise ValueError('Prepared update differs from the accepted release')
    updated = selected_package(store, selection)
    # Run the same noninteractive version probe used for portable smoke checks.
    environment = dict(os.environ)
    environment['LD_LIBRARY_PATH'] = str(updated)
    environment.pop('LD_PRELOAD', None)
    environment.pop('LD_AUDIT', None)
    if progress:
        progress({'phase': 'probe'})
    subprocess.run([str(updated / 'new-horizons'), '--version'], cwd=updated,
                   env=environment, check=True, capture_output=True, timeout=20)
    if cancel is not None and cancel.is_set():
        raise InterruptedError('Update cancelled before activation')
    atomic_selection(store, selection)
    return updated


def run_worker(worker, store, request, timeout=1800, progress=None, cancel=None):
    command = [sys.executable, str(worker), '--store', str(store), '--request', str(request)]
    if progress:
        command.append('--progress')
    def receive(line):
        try:
            value = json.loads(line)
        except ValueError:
            return
        if progress and isinstance(value, dict) and value.get('phase') in ('check', 'download', 'extract', 'verify'):
            progress(value)
    result = run_process(command, on_stderr=receive, cancel=cancel, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr[-2000:] or 'Update verification failed')
    return json.loads(result.stdout)


def current_package(original, store):
    try:
        return selected_package(store, small_json(store / 'current.json'))
    except FileNotFoundError:
        return original
    except (OSError, ValueError, KeyError, TypeError):
        print('New Horizons: Could not open the selected update. Using the original installation.', file=sys.stderr)
        return original


def choose_runtime(original):
    store = original / STORE_NAME
    try:
        plain(original, directory=True)
        plain(original / 'BUILD-IDENTITY.json')
        status = plain(store, directory=True).stat()
        if status.st_uid != os.getuid() or status.st_mode & 0o077:
            return original
        return current_package(original, store)
    except OSError:
        # A read-only installation or another launch can still play normally.
        return original


@contextmanager
def update_lock(original):
    store = original / STORE_NAME
    plain(original, directory=True)
    private_directory(store)
    descriptor = os.open(store / 'lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'rb') as lock:
        info = os.fstat(lock.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise OSError('Update lock must be a private owned regular file')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield store


def check_release(current, opener=None):
    release = latest_release(opener or build_opener(ReleaseRedirects()).open, timeout=5)
    if not isinstance(release, dict):
        raise ValueError('Invalid official release response')
    if release.get('draft') is not False or release.get('prerelease') is not False:
        raise ValueError('Expected an official stable release')
    tag = release.get('tag_name', '')
    wanted = tag.removeprefix('v') if isinstance(tag, str) else ''
    if not VERSION.fullmatch(wanted) or len(wanted) > 32:
        raise ValueError('Invalid official release version')
    if tuple(map(int, wanted.split('.'))) <= version(current):
        return None
    assets = release.get('assets', [])
    if not isinstance(assets, list) or len(assets) > 128:
        raise ValueError('Invalid official release assets')
    candidates = [asset for asset in assets if isinstance(asset, dict)
                  and isinstance(asset.get('name'), str) and ASSET_NAME.fullmatch(asset['name'])]
    if len(candidates) != 1:
        raise ValueError('The latest release has no unambiguous Linux update package')
    asset = candidates[0]
    digest = asset.get('digest', '')
    if not isinstance(digest, str) or not digest.startswith('sha256:') or not DIGEST.fullmatch(digest[7:]):
        raise ValueError('The official release has no verified Linux package digest')
    offer = {'format': 1, 'version': wanted, 'name': asset['name'], 'url': asset.get('browser_download_url'),
             'bytes': asset.get('size'), 'sha256': digest[7:]}
    if type(offer['bytes']) is not int or not 0 < offer['bytes'] <= MAX_ARCHIVE_BYTES:
        raise ValueError('Invalid official package size')
    validate_release_metadata(offer, release)
    return offer


def check_release_bounded(current, cancel=None):
    """A separate owned process bounds DNS and all I/O, not just each read."""
    command = [sys.executable, str(Path(__file__).absolute()), '--package', str(current), '--check-release']
    result = run_process(command, cancel=cancel, timeout=5)
    if result.returncode:
        raise RuntimeError(result.stderr[-1500:] or 'Could not check the official release')
    if len(result.stdout) > 16384:
        raise ValueError('Update-check response exceeds the launcher limit')
    offer = json.loads(result.stdout)
    if offer is not None and not isinstance(offer, dict):
        raise ValueError('Invalid update-check response')
    return offer


def install_release(original, offer, progress=None, cancel=None):
    with update_lock(original) as store:
        descriptor, name = tempfile.mkstemp(prefix='.accepted-update-', suffix='.json', dir=store)
        request = Path(name)
        try:
            with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
                json.dump(offer, stream)
                stream.flush()
                os.fsync(stream.fileno())
            return prepare_update(original, store, request, current_package(original, store), progress, cancel)
        finally:
            request.unlink(missing_ok=True)


def profile_path():
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'new-horizons-play'


def setup_command(package, forward=()):
    return [sys.executable, str(package / 'linux_assets_setup.py'),
            '--launcher', str(package / 'new-horizons-launch.sh'), '--client', str(package / 'new-horizons'),
            '--resources', str(package), '--profile', str(profile_path()), *forward]


def automatic_checks():
    try:
        path = plain(profile_path() / 'config/vcmi/settings.json')
        with path.open('rb') as stream:
            data = stream.read(1024 * 1024 + 1)
        if len(data) > 1024 * 1024:
            raise ValueError('Settings file exceeds the launcher limit')
        return json.loads(data).get('general', {}).get('updateChecks') is not False
    except (OSError, ValueError, AttributeError):
        return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--check-release', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    forward = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
    original = args.package.absolute()
    if args.check_release:
        if forward:
            raise ValueError('Update checks do not accept game arguments')
        print(json.dumps(check_release(original)))
        return 0
    package = choose_runtime(original)
    if not forward:
        from PortableLauncherWindow import run_window
        return run_window(original, package)
    environment = dict(os.environ)
    environment.pop('NH_PORTABLE_UPDATE_REQUEST', None)
    os.execve(sys.executable, setup_command(package, forward), environment)


if __name__ == '__main__':
    raise SystemExit(main())
