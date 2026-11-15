#!/usr/bin/env python3
"""Add the manual picker to one audited release; never compile or replace game PEs."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import zipfile

from package_new_horizons_windows import SYSTEM_DLLS, run
from windows_portable_layout import audit_bootstrap

PAYLOAD_COMMIT = 'a37f93bb13d9582f44f2d15cc5ba933b1c15ec88'
PAYLOAD_SHA256 = 'e264e86cebf4e9a5a65f1b479468bfabeca2b11346ffca2575c2b28f316fb354'
PAYLOAD_PREFIX = 'New-Horizons-Windows-x64-a37f93bb13d9/'
CHECKSUMS = 'runtime/SHA256SUMS.txt'
REPLACED = {'runtime/Start-New-Horizons.ps1'}
ADDED = {'Setup.exe', 'runtime/Complete-Assets.ps1', 'runtime/MANUAL-SETUP-IDENTITY.json'}
SOURCES = (
    'license.txt', 'win/bootstrap/NewHorizonsBootstrap.cpp', 'win/bootstrap/BootstrapArguments.h',
    'win/bootstrap/CMakeLists.txt', 'win/WindowsFileInfo.rc.in', 'clientapp/icons/new-horizons.ico',
    'cmake_modules/VersionDefinition.cmake', 'tools/windows/Start-New-Horizons.ps1',
    'tools/windows/Complete-Assets.ps1', 'tools/windows/Launch-Process.ps1',
    'tools/windows/Setup-Progress.ps1', 'tools/windows/tests/Manual-Setup-Smoke.ps1',
    'tools/windows/tests/Setup-Smoke.ps1', 'tools/ci/package_windows_manual_setup.py',
    'tools/ci/windows_portable_layout.py', 'tools/ci/package_new_horizons_windows.py',
    'tools/ci/source_export_policy.py', 'tools/nhart.py',
    'tools/tests/test_windows_manual_setup.py', '.github/workflows/new-horizons-windows-manual-setup.yml',
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_payload(path):
    if digest(path.read_bytes()) != PAYLOAD_SHA256:
        raise ValueError('Input ZIP is not the exact audited a37 release')
    payload = {}
    with zipfile.ZipFile(path) as archive:
        seen = set()
        for info in archive.infolist():
            name = info.filename
            parts = PurePosixPath(name).parts
            if (not name.startswith(PAYLOAD_PREFIX) or '\\' in name or ':' in name
                    or '..' in parts or name.startswith('/') or name.casefold() in seen
                    or stat.S_ISLNK(info.external_attr >> 16)):
                raise ValueError('Unsafe or duplicate release member')
            seen.add(name.casefold())
            if not info.is_dir():
                payload[name[len(PAYLOAD_PREFIX):]] = archive.read(info)
    verify_checksums(payload)
    if len(payload) != 1027 or set(payload) & ADDED:
        raise ValueError('Unexpected baseline payload membership')
    roots = {PurePosixPath(name).parts[0] for name in payload}
    if roots != {'new-horizons.exe', 'runtime'}:
        raise ValueError('Unexpected baseline root')
    return payload


def verify_checksums(payload):
    declared = {}
    for line in payload[CHECKSUMS].decode('utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]{64})  (.+)', line)
        if not match or match[2] in declared:
            raise ValueError('Malformed or repeated checksum')
        declared[match[2]] = match[1].lower()
    actual = {name: digest(data) for name, data in payload.items() if name != CHECKSUMS}
    if declared != actual:
        raise ValueError('Release checksum membership/content mismatch')


def git_source(root, commit):
    if not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('Exact packaging source commit required')
    return {name: subprocess.check_output(['git', '-C', str(root), 'show', commit + ':' + name])
            for name in SOURCES}


def compose(original, sources, setup, commit, imports):
    payload = dict(original)
    payload['runtime/Start-New-Horizons.ps1'] = sources['tools/windows/Start-New-Horizons.ps1']
    payload['runtime/Complete-Assets.ps1'] = sources['tools/windows/Complete-Assets.ps1']
    payload['Setup.exe'] = setup
    identity = {'format': 1, 'purpose': 'manual-original-assets-picker',
                'payload_source_commit': PAYLOAD_COMMIT, 'payload_zip_sha256': PAYLOAD_SHA256,
                'packaging_source_commit': commit, 'setup_sha256': digest(setup),
                'baseline_manifest_sha256': digest(original[CHECKSUMS]),
                'unchanged_pe_sha256': {name: digest(data) for name, data in original.items()
                                        if Path(name).suffix.lower() in {'.exe', '.dll'}},
                'setup_source_files_sha256': {name: digest(data) for name, data in sources.items()},
                'setup_system_imports': imports,
                'replaced_payload_paths': sorted(REPLACED), 'added_payload_paths': sorted(ADDED),
                'engine_rebuilt': False,
                'approved_file_delta': {name: {'before': digest(original[name]) if name in original else None,
                                                'after': digest(payload[name])}
                                        for name in sorted(REPLACED | (ADDED - {'runtime/MANUAL-SETUP-IDENTITY.json'}))}}
    payload['runtime/MANUAL-SETUP-IDENTITY.json'] = (json.dumps(identity, indent=2, sort_keys=True) + '\n').encode()
    payload[CHECKSUMS] = ''.join(f'{digest(data)}  {name}\n' for name, data in sorted(payload.items())
                               if name != CHECKSUMS).encode()
    if set(payload) - set(original) != ADDED:
        raise ValueError('Unapproved added payload')
    changed = {name for name in original if payload[name] != original[name]}
    if changed != REPLACED | {CHECKSUMS}:
        raise ValueError('Unexpected old payload change')
    verify_checksums(payload)
    return payload


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--setup', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--packaging-commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    original = read_payload(args.payload)
    if args.setup.is_symlink() or not args.setup.is_file():
        raise ValueError('Setup must be an actual ordinary build output')
    dumpbin = shutil.which('dumpbin')
    if not dumpbin:
        raise RuntimeError('dumpbin required for independent Setup.exe system-import audit')
    imports = audit_bootstrap(args.setup, run, SYSTEM_DLLS, dumpbin)
    sources = git_source(args.source_root, args.packaging_commit)
    payload = compose(original, sources, args.setup.read_bytes(), args.packaging_commit, imports)
    args.output.mkdir(parents=True, exist_ok=False)
    stem = 'New-Horizons-Windows-x64-manual-setup-' + args.packaging_commit[:12]
    with zipfile.ZipFile(args.output / (stem + '.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            archive.writestr(stem + '/' + name, data)
    with tarfile.open(args.output / 'manual-setup-source.tar.gz', 'w:gz') as archive:
        for name, data in sources.items():
            member = tarfile.TarInfo('manual-setup-source/' + name)
            member.size, member.mode = len(data), 0o644
            archive.addfile(member, io.BytesIO(data))
    shutil.copyfile(args.setup, args.output / 'Setup.exe')
    checksums = [f'{digest(path.read_bytes())}  {path.name}\n' for path in sorted(args.output.iterdir())]
    (args.output / 'SHA256SUMS.txt').write_text(''.join(checksums), encoding='ascii')
    print('Ready: unchanged released game payload plus manual Setup and corresponding setup sources.')


if __name__ == '__main__':
    main()
