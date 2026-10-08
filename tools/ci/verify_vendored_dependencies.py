#!/usr/bin/env python3
"""Offline integrity gate for the preserved dependency recipe snapshot."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

ORIGIN = 'https://github.com/vcmi/vcmi-dependencies.git'
REVISION = 'f61de7b5e1cbfc3d916a600b7468b5d9c2492995'
MANIFEST_SHA256 = '5e78036b39b6c466f5804ed6fc3c35d2c89438a9375d8d6f244ea03c167acce7'
LOCAL_METADATA = {'UPSTREAM.json', 'VENDORED.md'}


def unique_pairs(pairs):
    data = {}
    for key, value in pairs:
        if key in data:
            raise ValueError('Duplicate dependency provenance key')
        data[key] = value
    return data


def verify(directory):
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Vendored dependency directory must be regular')
    for name in LOCAL_METADATA:
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing or linked dependency provenance')
    manifest = json.loads((directory / 'UPSTREAM.json').read_text(), object_pairs_hook=unique_pairs)
    fingerprint = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if fingerprint != MANIFEST_SHA256:
        raise ValueError('Dependency provenance fingerprint differs from pinned snapshot')
    if (type(manifest.get('format')) is not int or manifest['format'] != 1 or manifest.get('origin') != ORIGIN
            or manifest.get('revision') != REVISION or not manifest.get('licenseStatus')):
        raise ValueError('Unexpected dependency source provenance')
    entries = manifest.get('files')
    if not isinstance(entries, dict) or not entries:
        raise ValueError('Missing dependency source inventory')
    total = 0
    for name, item in entries.items():
        relative = PurePosixPath(name)
        if (relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name
                or relative.as_posix() != name or name in LOCAL_METADATA or '.git' in relative.parts):
            raise ValueError('Unsafe dependency source path')
        if (not isinstance(item, dict) or set(item) != {'size', 'sha256'}
                or type(item['size']) is not int or item['size'] < 0
                or not isinstance(item['sha256'], str)
                or not re.fullmatch('[0-9a-f]{64}', item['sha256'])):
            raise ValueError('Invalid dependency source fingerprint')
        path = directory / name
        if any((directory / Path(*relative.parts[:i])).is_symlink() for i in range(1, len(relative.parts) + 1)):
            raise ValueError('Linked dependency source')
        if (not path.is_file() or path.stat().st_size != item['size']
                or hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']):
            raise ValueError('Changed or missing dependency source: ' + name)
        total += item['size']
    actual = set()
    for path in directory.rglob('*'):
        name = path.relative_to(directory).as_posix()
        # A current pre-conversion checkout can still carry its local Git pointer.
        # Git metadata is never an upstream source file or part of the inventory.
        if '.git' in path.relative_to(directory).parts:
            continue
        if path.is_symlink():
            raise ValueError('Linked dependency source')
        if path.is_file() and name not in LOCAL_METADATA:
            actual.add(name)
    if actual != set(entries):
        raise ValueError('Unexpected dependency source inventory')
    return {'revision': REVISION, 'files': len(entries), 'bytes': total}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'dependencies')
    args = parser.parse_args()
    result = verify(args.directory)
    print('VERIFIED: {files} vendored dependency source files, {bytes} bytes, revision {revision}'.format(**result))


if __name__ == '__main__':
    main()
