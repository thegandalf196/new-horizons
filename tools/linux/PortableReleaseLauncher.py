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
import time

from PortableReleaseUpdate import (ASSET_NAME, CHANNELS, MAX_ARCHIVE_BYTES, PACKAGE_ROOT as INSTALL_NAME,
                                   ReleaseRedirects, latest_release, private_directory, recent_releases,
                                   validate_release_metadata, verify_player_files)
from urllib.request import build_opener
from PortableLauncherProcess import run_process
import PortableLauncherLog as launcher_log
import PortableReleaseNotes as release_notes

STORE_NAME = '.nh-player-updates'
# Each track selects its own verified package and plays with its own profile.
SELECTIONS = {'stable': 'current.json', 'beta': 'beta.json'}
PROFILES = {'stable': 'new-horizons-play', 'beta': 'new-horizons-play-beta'}
SELF_TEST = 'NH_PORTABLE_LAUNCHER_V2'
# Set when an older installed launcher handed over to this newer copy.
CODE_ROOT = None
VERSION = re.compile(r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)')
DIGEST = re.compile(r'[0-9a-f]{64}')
MAX_IDENTITY = 2 * 1024 * 1024  # A release identity, which lists every packaged library.
# Two capped notes, JSON-escaped, plus the offer.
MAX_NOTES_RESPONSE = 512 * 1024
# The whole owned check child: interpreter start, one GitHub request and parsing.
# Play never waits for it.
CHECK_DEADLINE = 45
# A stalled GitHub request fails with its own error before that deadline.
REQUEST_TIMEOUT = 20
# Phases the check child reports, and how a timeout names the step still running.
CHECK_PHASES = {'launch': 'starting the update check', 'request': 'contacting GitHub',
                'release': 'checking the published release', 'notes': 'reading release notes'}


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


def package_channel(package):
    """The track a package was published on. Packages without the marker predate it and are stable."""
    try:
        with plain(package / 'BUILD-IDENTITY.json').open('rb') as source:
            data = source.read(MAX_IDENTITY + 1)
        identity = json.loads(data) if len(data) <= MAX_IDENTITY else None
    except (OSError, ValueError):
        return 'stable'
    return 'beta' if isinstance(identity, dict) and identity.get('release_channel') == 'beta' else 'stable'


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


def atomic_selection(store, selection, name='current.json'):
    fd, filename = tempfile.mkstemp(prefix='.activate-', dir=store)
    staging = Path(filename)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(selection, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(staging, store / name)
    finally:
        staging.unlink(missing_ok=True)


def prepare_update(original, store, request, current, progress=None, cancel=None, channel='stable'):
    wanted = small_json(request)
    if not isinstance(wanted, dict) or not isinstance(wanted.get('version'), str):
        raise ValueError('Invalid update request')
    release = wanted['version']
    if len(release) > 32 or not VERSION.fullmatch(release):
        raise ValueError('Invalid requested version')
    if current is not None and tuple(map(int, release.split('.'))) <= version(current):
        return current
    worker = plain((CODE_ROOT or original) / 'PortableReleaseUpdate.py')
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
    started = time.monotonic()
    # The first start of a new executable can be slow on a busy system.
    probe = subprocess.run([str(updated / 'new-horizons'), '--version'], cwd=updated,
                           env=environment, check=False, capture_output=True, timeout=60)
    launcher_log.write(f'New client --version check exited with code {probe.returncode} '
                       f'in {int((time.monotonic() - started) * 1000)} ms')
    probe.check_returncode()
    if cancel is not None and cancel.is_set():
        raise InterruptedError('Update cancelled before activation')
    atomic_selection(store, selection, SELECTIONS[channel])
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
    result = run_process(command, launcher_log.child_environment(), on_stderr=receive, cancel=cancel, timeout=timeout)
    launcher_log.write(f'Update worker exited with code {result.returncode}')
    if result.returncode:
        raise RuntimeError(result.stderr[-2000:] or 'Update verification failed')
    return json.loads(result.stdout)


def current_package(original, store, channel='stable'):
    """A track falls back to the original install when published on it; the other track starts empty."""
    fallback = original if package_channel(original) == channel else None
    try:
        return selected_package(store, small_json(store / SELECTIONS[channel]))
    except FileNotFoundError:
        return fallback
    except (OSError, ValueError, KeyError, TypeError):
        print(f'New Horizons: Could not open the selected {channel} update.', file=sys.stderr)
        return fallback


def choose_runtime(original, channel='stable'):
    store = original / STORE_NAME
    fallback = original if package_channel(original) == channel else None
    try:
        plain(original, directory=True)
        plain(original / 'BUILD-IDENTITY.json')
        status = plain(store, directory=True).stat()
        if status.st_uid != os.getuid() or status.st_mode & 0o077:
            return fallback
        return current_package(original, store, channel)
    except OSError:
        # A read-only installation or another launch can still play normally.
        return fallback


def track_baseline(original, channel):
    """The package an offer must be newer than: the installed stable, or the newer of both
    tracks for a beta. None when stable starts empty (a beta package): any stable is offered."""
    packages = [choose_runtime(original)] + ([choose_runtime(original, 'beta')] if channel == 'beta' else [])
    return max(filter(None, packages), key=version, default=None)


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


def release_version(release):
    tag = release.get('tag_name', '') if isinstance(release, dict) else ''
    wanted = tag.removeprefix('v') if isinstance(tag, str) else ''
    return wanted if len(wanted) <= 32 and VERSION.fullmatch(wanted) else None


def newest_beta(releases):
    """The newest published release of either kind that carries a Linux package."""
    def eligible(release):
        return (isinstance(release, dict) and release.get('draft') is False and release_version(release)
                and isinstance(release.get('assets'), list)
                and sum(isinstance(asset, dict) and isinstance(asset.get('name'), str)
                        and bool(ASSET_NAME.fullmatch(asset['name'])) for asset in release['assets']) == 1)
    candidates = [release for release in releases if eligible(release)]
    return max(candidates, key=lambda release: tuple(map(int, release_version(release).split('.'))), default=None)


def check_release(current, opener=None, channel='stable', notes=None):
    """notes, when given, maps wanted versions to None. The same response fills
    it with the plain notes of those versions and of the offer."""
    opener = opener or build_opener(ReleaseRedirects()).open
    launcher_log.phase('request')
    if channel == 'beta':
        releases = recent_releases(opener, timeout=REQUEST_TIMEOUT)
        release = newest_beta(releases)
    else:
        release = latest_release(opener, timeout=REQUEST_TIMEOUT)
        releases = [release]
    launcher_log.phase('release')
    offer = None if release is None else release_offer(current, release, channel)
    if notes is not None:
        launcher_log.phase('notes')
        notes.update(release_notes.retained(releases, {*notes, offer and offer['version']}))
    return offer


def release_offer(current, release, channel):
    if not isinstance(release, dict):
        raise ValueError('Invalid official release response')
    if release.get('draft') is not False or (channel == 'stable' and release.get('prerelease') is not False):
        raise ValueError('Expected an official stable release')
    wanted = release_version(release)
    if wanted is None:
        raise ValueError('Invalid official release version')
    if current is not None and tuple(map(int, wanted.split('.'))) <= version(current):
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
    if channel == 'beta':
        offer.update(format=2, channel='beta')
    if type(offer['bytes']) is not int or not 0 < offer['bytes'] <= MAX_ARCHIVE_BYTES:
        raise ValueError('Invalid official package size')
    validate_release_metadata(offer, release)
    return offer


def check_release_bounded(current, cancel=None, channel='stable'):
    """A separate owned process bounds DNS and all I/O, not just each read."""
    offer = bounded_check(current, cancel, channel, (), 16384)
    if offer is not None and not isinstance(offer, dict):
        raise ValueError('Invalid update-check response')
    return offer


def check_release_notes_bounded(current, installed, cancel=None, channel='stable'):
    """The same check, also returning plain release notes of the installed and offered versions."""
    wanted = [installed] if installed else []
    result = bounded_check(current, cancel, channel, ('--notes-for', *wanted), MAX_NOTES_RESPONSE)
    if not isinstance(result, dict) or set(result) != {'offer', 'notes'}:
        raise ValueError('Invalid update-check response')
    offer, notes = result['offer'], result['notes']
    if offer is not None and not isinstance(offer, dict) or not isinstance(notes, dict):
        raise ValueError('Invalid update-check response')
    return offer, {key: value for key, value in notes.items() if isinstance(key, str) and VERSION.fullmatch(key)
                   and isinstance(value, str) and len(value) <= release_notes.MAX_CHARS + 64}


def bounded_check(current, cancel, channel, extra, limit, deadline=CHECK_DEADLINE):
    # Without a baseline the child takes any release; --package then only names this code.
    floor = ('--package', str(current)) if current else ('--package', str(Path(__file__).absolute().parent), '--any-version')
    command = [sys.executable, str(Path(__file__).absolute()), *floor, '--check-release', '--channel', channel, *extra]
    phase, errors = ['launch'], []
    def receive(line):
        try:
            value = json.loads(line)
        except ValueError:
            value = None
        if isinstance(value, dict) and value.get('phase') in CHECK_PHASES:
            phase[0] = value['phase']
        elif line.strip():
            errors.append(line)
    started = time.monotonic()
    launcher_log.write(f'Check child started for the {channel} track; deadline {deadline} s')
    try:
        result = run_process(command, launcher_log.child_environment(), on_stderr=receive, cancel=cancel,
                             timeout=deadline)
    except InterruptedError:
        if cancel is not None and cancel.is_set():
            launcher_log.write('Check stopped by the player')
            raise
        launcher_log.write(f'Check deadline of {deadline} s reached after {int((time.monotonic() - started) * 1000)} ms; '
                           f'the child was in phase {phase[0]} and was stopped')
        raise TimeoutError(f'Update check timed out while {CHECK_PHASES[phase[0]]} after {deadline} s. '
                           'You can still play the installed game.') from None
    launcher_log.write(f'Check child exited with code {result.returncode} after '
                       f'{int((time.monotonic() - started) * 1000)} ms')
    if result.returncode:
        raise RuntimeError(''.join(errors)[-1500:] or 'Could not check the official release')
    if len(result.stdout) > limit:
        raise ValueError('Update-check response exceeds the launcher limit')
    return json.loads(result.stdout)


def install_release(original, offer, progress=None, cancel=None):
    channel = offer.get('channel', 'stable')
    if channel not in CHANNELS:
        raise ValueError('Unknown release track')
    with update_lock(original) as store:
        descriptor, name = tempfile.mkstemp(prefix='.accepted-update-', suffix='.json', dir=store)
        request = Path(name)
        try:
            with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
                json.dump(offer, stream)
                stream.flush()
                os.fsync(stream.fileno())
            # Newer than the track's own package; an empty beta beats stable, an empty stable takes any.
            current = current_package(original, store, channel)
            if current is None and channel == 'beta':
                current = current_package(original, store)
            return prepare_update(original, store, request, current, progress, cancel, channel)
        finally:
            request.unlink(missing_ok=True)


def profile_path(channel='stable'):
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / PROFILES[channel]


def remembered_assets(channel):
    """The path a track's setup remembered, read without trusting its contents."""
    try:
        path = plain(profile_path(channel).with_name(PROFILES[channel] + '.setup') / 'original-assets.json')
        value = small_json(path)
        assets = value.get('assets') if isinstance(value, dict) else value
        return assets if isinstance(assets, str) and assets.startswith('/') else None
    except (OSError, ValueError):
        return None


def setup_command(package, forward=(), channel='stable'):
    command = [sys.executable, str(package / 'linux_assets_setup.py'),
               '--launcher', str(package / 'new-horizons-launch.sh'), '--client', str(package / 'new-horizons'),
               '--resources', str(package), '--profile', str(profile_path(channel))]
    # Beta keeps separate saves but reuses the stable original-game selection
    # until the player chooses a different folder for it. Setup validates it.
    if channel == 'beta' and '--select-assets' not in forward and remembered_assets('beta') is None:
        stable = remembered_assets('stable')
        if stable:
            command += ['--assets', stable]
    return [*command, *forward]


def automatic_checks(channel='stable'):
    try:
        path = plain(profile_path(channel) / 'config/vcmi/settings.json')
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
    parser.add_argument('--channel', choices=CHANNELS, default='stable', help=argparse.SUPPRESS)
    parser.add_argument('--notes-for', nargs='*', help=argparse.SUPPRESS)
    parser.add_argument('--any-version', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--delegated', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--self-test', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    forward = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
    original = args.package.absolute()
    if args.self_test:
        print(SELF_TEST)
        return 0
    if args.check_release:
        if forward:
            raise ValueError('Update checks do not accept game arguments')
        return check_child(None if args.any_version else original, args.channel, args.notes_for)
    if not forward:
        # The window's session log starts before a hand-over and records its self-test.
        code = Path(__file__).absolute().parent
        launcher_log.start_session()
        launcher_log.write_header(launcher_version(code), code)
    if args.delegated:
        global CODE_ROOT
        CODE_ROOT = Path(__file__).absolute().parent
    else:
        newer = newer_launcher(original)
        if newer:
            environment = launcher_log.child_environment()
            environment.pop('NH_PORTABLE_UPDATE_REQUEST', None)
            os.execve(sys.executable, [sys.executable, '-B', str(newer), '--package', str(original),
                                       '--delegated', '--', *forward], environment)
    # An installation plays its own track: a beta package plays the beta profile.
    own = package_channel(original)
    package = choose_runtime(original, own)
    if not forward:
        launcher_log.write(f'Installation {original} ({own} package); {own} plays {package}')
        from PortableLauncherWindow import run_window
        return run_window(original, package)
    environment = dict(os.environ)
    environment.pop('NH_PORTABLE_UPDATE_REQUEST', None)
    os.execve(sys.executable, setup_command(package, forward, own), environment)


def launcher_version(code):
    try:
        return '.'.join(map(str, version(code)))
    except (OSError, ValueError, KeyError):
        return 'unknown'


def newer_launcher(original):
    """Hand over to the launcher inside the selected update of the installation's own
    track, so launcher improvements arrive with updates. Any doubt keeps this launcher."""
    package = choose_runtime(original, package_channel(original))
    if package == original:
        return None
    started = time.monotonic()
    try:
        code = plain(package / 'PortableReleaseLauncher.py')
        result = subprocess.run([sys.executable, '-B', str(code), '--package', str(original), '--self-test'],
                                cwd=package, capture_output=True, text=True, timeout=10, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        launcher_log.write(f'Launcher self-test of {package} failed: {launcher_log.describe_error(error)}')
        return None
    passed = result.returncode == 0 and result.stdout.strip() == SELF_TEST
    launcher_log.write(f'Launcher self-test of {package} exited with code {result.returncode} in '
                       f'{int((time.monotonic() - started) * 1000)} ms; {"passed" if passed else "failed"}')
    return code if passed else None


def check_child(original, channel, notes_for):
    """The owned update-check process: one JSON answer on stdout, phases on stderr."""
    if notes_for is not None and (len(notes_for) > 2 or not all(VERSION.fullmatch(value) for value in notes_for)):
        raise ValueError('Invalid release notes version')
    launcher_log.join_session('check', report_phases=True)
    started = time.monotonic()
    launcher_log.write(f'Check child pid {os.getpid()} running; Python {sys.version.split()[0]}, track {channel}')
    try:
        if notes_for is None:
            answer = check_release(original, channel=channel)
        else:
            notes = dict.fromkeys(notes_for)
            offer = check_release(original, channel=channel, notes=notes)
            answer = {'offer': offer, 'notes': {key: value for key, value in notes.items() if value}}
    except Exception as error:
        launcher_log.write(f'Check failed after {int((time.monotonic() - started) * 1000)} ms: '
                           f'{launcher_log.describe_error(error)}')
        print(f'Could not check the official release: {launcher_log.describe_error(error)}', file=sys.stderr)
        return 1
    found = (answer.get('offer') if notes_for is not None else answer) or {}
    launcher_log.write(f'Check succeeded in {int((time.monotonic() - started) * 1000)} ms: '
                       + (f'version {found["version"]}' if found else 'nothing newer'))
    print(json.dumps(answer))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
