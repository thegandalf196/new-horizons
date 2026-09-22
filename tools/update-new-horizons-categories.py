#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose a private full-roster category diagnostic; never replace the live module."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    build = (ROOT / 'build').resolve()
    try:
        output.relative_to(build)
    except ValueError:
        parser.error('preview output must remain under build/')
    if args.output.is_symlink() or (output.exists() and not args.check):
        parser.error('refusing existing output or symlink')
    build.mkdir(exist_ok=True)
    metadata = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
    rules = json.loads((ROOT / 'config/newHorizonsCreatureCategories.json').read_text())
    texts = json.loads((ROOT / 'config/newHorizonsCreatureCategoryTexts.json').read_text())
    if metadata['version'] != '0.11.0' or metadata['settings'].get('creatures') != {'newHorizonsCategories': rules}:
        parser.error('unexpected live category composition; regenerate the active module first')
    if any(metadata['translations'].get(key) != value for key, value in texts.items()):
        parser.error('live category translations are missing or stale')
    metadata['name'] = 'New Horizons (category diagnostic)'
    metadata['version'] = '0.11.1'
    metadata['description'] += (
        ' This private category diagnostic mirrors the complete active standard-faction'
        ' Core/Elite/Champion table. Recruitment row identity, upgrades, statistics,'
        ' capacity and AI values remain unchanged.')
    text = json.dumps(metadata, indent='\t', ensure_ascii=False) + '\n'
    if args.check:
        if not output.is_file() or output.read_text() != text:
            parser.error('preview differs from current canonical composition')
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('x', encoding='utf-8') as stream:
            stream.write(text)
    print(output)


if __name__ == '__main__':
    main()
