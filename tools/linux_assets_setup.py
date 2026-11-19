#!/usr/bin/env python3
"""Select original Complete data before entering the managed Linux launcher.

The companion <profile>.setup directory contains only the remembered source.
Original files are referenced, not copied; the managed launcher remains the
owner of runtime links, saves, locking, child processes and crash reporting.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

ARCHIVES = ('h3bitmap.lod', 'h3sprite.lod', 'h3ab_bmp.lod', 'h3ab_spr.lod')
PICKER_NAME = 'new-horizons-setup'


def matching_child(directory, name, folder):
    exact = directory / name
    matches = [exact] if exact.exists() else [p for p in directory.iterdir() if p.name.casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError(f'Missing or ambiguous original {name}. Select the Complete installation folder.')
    path = matches[0]
    if folder:
        valid = path.is_dir() and os.access(path, os.R_OK | os.X_OK)
    else:
        valid = path.is_file() and path.stat().st_size > 0 and os.access(path, os.R_OK)
    if not valid:
        raise ValueError(f'Original {name} is unreadable or empty.')
    return path


def validate_assets(value):
    root = Path(value).expanduser().resolve(strict=True)
    if not root.is_dir():
        raise ValueError('Select the Heroes III Complete installation folder.')
    data = matching_child(root, 'Data', True)
    matching_child(root, 'Maps', True)
    matching_child(root, 'Mp3', True)
    for name in ARCHIVES:
        matching_child(data, name, False)
    return root


def state_file(profile):
    return profile.with_name(profile.name + '.setup') / 'original-assets.json'


def safe_state_directory(path):
    if path.is_symlink() or (path.exists() and not path.is_dir()):
        raise ValueError('The setup settings directory must be a private regular directory.')
    if path.exists():
        info = path.stat()
        if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077:
            raise ValueError('The setup settings directory must be owned by you and private (0700).')


def read_selection(profile):
    path = state_file(profile)
    safe_state_directory(path.parent)
    if not path.exists() and not path.is_symlink():
        return None
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077:
        raise ValueError('The saved original-game selection must be a private regular file.')
    if info.st_size > 16384:
        raise ValueError('The saved original-game selection is invalid.')
    try:
        value = json.loads(path.read_text())
    except (ValueError, UnicodeError) as error:
        raise ValueError('The saved original-game selection is invalid.') from error
    if not isinstance(value, dict) or set(value) != {'version', 'assets'} or value['version'] != 1 or not isinstance(value['assets'], str):
        raise ValueError('The saved original-game selection is invalid.')
    return value['assets']


def separated(path, inputs):
    if path == Path('/'):
        raise ValueError('The filesystem root cannot be a profile.')
    for source in inputs:
        if path == source or source in path.parents or path in source.parents:
            raise ValueError('The writable profile/setup settings must not overlap the game or original files.')


def write_selection(profile, assets):
    path = state_file(profile)
    safe_state_directory(path.parent)
    path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            json.dump({'version': 1, 'assets': str(assets)}, stream, ensure_ascii=False)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def picker_path(args):
    path = args.client.parent / PICKER_NAME
    if path.is_symlink() or not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError('This build has no desktop setup helper. Run with --assets followed by your Complete folder.')
    return path


def notify(picker, message, error=False):
    subprocess.run([str(picker), '--error' if error else '--info', message], check=False)


def choose(args, previous):
    picker = picker_path(args)
    if not os.environ.get('DISPLAY') and not os.environ.get('WAYLAND_DISPLAY'):
        raise ValueError('No desktop display is available. Supply --assets for a terminal or headless launch.')
    notify(picker, 'Select your Heroes III Complete installation folder containing Data, Maps and Mp3. '
           'New Horizons will remember this location and leave the original files unchanged.')
    initial = previous or args.initial_assets or str(Path.home())
    while True:
        result = subprocess.run([str(picker), '--choose-folder', initial], capture_output=True, check=False)
        if result.returncode == 2:
            return None
        if result.returncode != 0:
            raise ValueError('The desktop folder chooser failed. Supply --assets from a terminal.')
        if not result.stdout.endswith(b'\0') or result.stdout.count(b'\0') != 1 or len(result.stdout) > 65536:
            raise ValueError('The desktop folder chooser returned an invalid path.')
        initial = os.fsdecode(result.stdout[:-1])
        try:
            return validate_assets(initial)
        except (ValueError, OSError) as error:
            notify(picker, str(error), True)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--resources', type=Path)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--assets')
    parser.add_argument('--initial-assets')
    parser.add_argument('--select-assets', action='store_true')
    parser.add_argument('--setup-only', action='store_true')
    parser.add_argument('--noninteractive', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--debugger-log')
    parser.add_argument('client_args', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    args.client = args.client.resolve(strict=True)
    args.resources = (args.resources or args.client.parent).resolve(strict=True)
    args.profile = args.profile.expanduser().resolve()
    args.launcher = args.launcher.resolve(strict=True)
    if args.client_args[:1] == ['--']:
        args.client_args = args.client_args[1:]
    return args


def run(args):
    inputs = [args.resources, args.client.parent]
    separated(args.profile, inputs)
    separated(state_file(args.profile).parent, inputs)
    previous = None if args.assets else read_selection(args.profile)
    noninteractive = args.noninteractive or args.verify_only or any(
        value.lower().split('=')[0] in ('--headless', '--disable-video') for value in args.client_args)
    remembered = False
    if args.assets:
        assets = validate_assets(args.assets)
    else:
        try:
            assets = validate_assets(previous) if previous and not args.select_assets else None
        except (ValueError, OSError):
            assets = None
        if assets is None:
            if noninteractive:
                raise ValueError('No valid original-game selection. Supply --assets or run desktop setup first.')
            assets = choose(args, previous)
            if assets is None:
                return 0
            remembered = True
    separated(args.profile, [assets])
    separated(state_file(args.profile).parent, [assets])
    command = [str(args.launcher), '--assets', str(assets), '--profile', str(args.profile),
               '--client', str(args.client), '--resources', str(args.resources)]
    if args.debugger_log:
        command += ['--debugger-log', args.debugger_log]
    # Let the maintained preflight admit all engine/profile paths before setup
    # writes anything. Its verify-only path neither starts a game nor mutates.
    result = subprocess.run(command + ['--verify-only'], check=False)
    if result.returncode:
        return result.returncode
    if args.verify_only:
        return 0
    if remembered or args.setup_only:
        write_selection(args.profile, assets)
    if args.setup_only:
        return 0
    if os.environ.get('NH_LAUNCHER_STATUS') == '1':
        print('NH_LAUNCHER_PREPARING_ART_V1', flush=True)
    os.execv(str(args.launcher), command + ['--', *args.client_args])


def main():
    try:
        return run(parse_args(sys.argv[1:]))
    except (ValueError, OSError) as error:
        print('New Horizons setup: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
