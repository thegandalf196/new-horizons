#!/usr/bin/env python3
"""Bring an installation's launcher up to the one inside its selected update.

Launchers older than 0.92.6 never hand over to an updated launcher: the only
updated code they run is the selected update's setup script, on Play. That
script calls upgrade_original() so the next start shows the current launcher.
Launchers that already hand over are left alone.
"""
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

STORE_NAME = '.nh-player-updates'
SELF_TEST = 'NH_PORTABLE_LAUNCHER_V2'
MARKER = f"SELF_TEST = '{SELF_TEST}'".encode()
# Helpers first and the entry point last, so an interrupted upgrade never
# starts a new entry point against old helpers.
FILES = ('PortableLauncherProcess.py', 'PortableReleaseUpdate.py', 'PortableReleaseNotes.py',
         'PortableLauncherWindow.py', 'PortableReleaseLauncher.py')
# Launcher files that older installations do not have yet. Restoring removes them.
ADDED_FILES = frozenset(('PortableReleaseNotes.py',))
BACKUP_NAME = 'launcher-backup'


def owned_file(path):
    status = path.lstat()
    if not stat.S_ISREG(status.st_mode) or status.st_uid != os.getuid():
        raise OSError(f'{path.name} is not a plain file owned by this user')
    return path


def locate_original(package):
    """Return the installation whose selected stable update is package, else None."""
    release = package.parent
    store = release.parent.parent
    if release.parent.name != 'releases' or store.name != STORE_NAME:
        return None
    selection = json.loads(owned_file(store / 'current.json').read_bytes()[:4096])
    if not isinstance(selection, dict) or selection.get('sha256') != release.name:
        return None
    return store.parent


def replace(source, target):
    data = source.read_bytes()
    compile(data, str(source), 'exec')
    staged = target.with_name('.' + target.name + '.nh-upgrade')
    with open(staged, 'wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    staged.chmod(0o644)
    os.replace(staged, target)


def self_test(original):
    result = subprocess.run([sys.executable, '-B', str(original / 'PortableReleaseLauncher.py'),
                             '--package', str(original), '--self-test'],
                            cwd=original, capture_output=True, text=True, timeout=10, check=False)
    return result.returncode == 0 and result.stdout.strip() == SELF_TEST


def existing(path, name):
    """Whether the installation has this launcher file; only added files may be missing."""
    if name in ADDED_FILES and not os.path.lexists(path / name):
        return False
    owned_file(path / name)
    return True


def install(package, original):
    status = original.lstat()
    if not stat.S_ISDIR(status.st_mode) or status.st_uid != os.getuid():
        raise OSError('The installation folder is not owned by this user')
    present = [name for name in FILES if existing(original, name)]
    for name in FILES:
        owned_file(package / name)
    backup = original / STORE_NAME / BACKUP_NAME
    backup.mkdir(mode=0o700, exist_ok=True)
    for name in present:
        shutil.copyfile(original / name, backup / name)
    try:
        for name in FILES:
            replace(package / name, original / name)
        if self_test(original):
            return 'upgraded'
    except (OSError, SyntaxError, ValueError, subprocess.SubprocessError):
        pass
    for name in FILES:
        if name in present:
            replace(backup / name, original / name)
        else:
            (original / name).unlink(missing_ok=True)
    print('New Horizons: the launcher update did not pass its check; the previous launcher was kept.',
          file=sys.stderr)
    return 'restored'


def upgrade_original(package):
    """Upgrade an old launcher once. Returns 'upgraded', 'restored', 'current'
    or None when nothing applies. Never raises; the game always starts."""
    try:
        package = Path(package)
        original = locate_original(package)
        if original is None:
            return None
        if MARKER in owned_file(original / 'PortableReleaseLauncher.py').read_bytes():
            return 'current'
        if MARKER not in owned_file(package / 'PortableReleaseLauncher.py').read_bytes():
            return None
        return install(package, original)
    except (OSError, SyntaxError, ValueError, subprocess.SubprocessError) as error:
        print(f'New Horizons: the launcher could not be updated ({error}). The game starts normally.',
              file=sys.stderr)
        return None
