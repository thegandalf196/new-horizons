#!/usr/bin/env python3
"""Verify final NHART installation bytes, no loose duplicates, and both mount scopes."""
import argparse
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'ci'))
from package_new_horizons_windows import verify_runtime_art, nhart, RUNTIME_ART_PACK


def read_jsonc(path):
    text = path.read_text(encoding='utf-8')
    text = re.sub(r'("(?:\\.|[^"\\])*")|(/\*.*?\*/|//[^\n]*)',
                  lambda match: match.group(1) if match.group(1) is not None else ' ',
                  text, flags=re.DOTALL)
    return nhart.read_json(text)


def require_mount(path, expected, overlay=False):
    if path.is_symlink() or not path.is_file():
        raise RuntimeError('Required regular mount configuration missing: ' + path.name)
    data = read_jsonc(path)
    filesystem = data.get('filesystem') if isinstance(data, dict) else None
    mounts = filesystem.get('') if isinstance(filesystem, dict) else None
    if not isinstance(mounts, list) or not mounts or any(not isinstance(row, dict) for row in mounts):
        raise RuntimeError('Missing root-scope NHART mount: ' + path.name)
    archives = [row for rows in filesystem.values() if isinstance(rows, list)
                for row in rows if isinstance(row, dict) and row.get('type') == 'nhart']
    selected = {'type': 'nhart', 'path': expected}
    if overlay:
        selected['overlay'] = True
    if (archives != [selected] or mounts[-1] != selected
            or (overlay and any(row.get('overlay') is not True for row in archives))):
        raise RuntimeError('Incorrect or ambiguous NHART mount: ' + path.name)


def verify_install(resources, manifest):
    resources = Path(resources)
    expected = nhart.read_json(Path(manifest).read_bytes())
    sources = verify_runtime_art(resources, expected)
    for name in sources:
        if (resources / name).exists():
            raise RuntimeError('Packed runtime art still installed loose: ' + name)
    require_mount(resources / 'config/filesystem.json', RUNTIME_ART_PACK, overlay=True)
    require_mount(resources / 'Mods/new-horizons/mod.json', '/NewHorizons.nhart')
    verified = nhart.verify(resources / RUNTIME_ART_PACK, expected)
    return {'format': verified['format'], 'selectedEntries': len(verified['manifest']['entries']),
            'packBytes': verified['size'], 'looseDuplicates': 0, 'mountsVerified': 2}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    result = verify_install(args.resources, args.manifest)
    print('VERIFIED: NHART format {format}, {selectedEntries} selected entries, '
          '{packBytes} bytes, no loose duplicates, both mounts'.format(**result))


if __name__ == '__main__':
    main()
