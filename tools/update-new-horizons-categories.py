#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose a private partial-Conflux category preview; never activate defaults."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

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
    with tempfile.TemporaryDirectory(prefix='nh-categories-', dir=build) as temporary:
        base = Path(temporary) / 'mod.json'
        subprocess.run([sys.executable, str(ROOT / 'tools/update-new-horizons-convenience.py'),
                        '--output', str(base)], check=True, capture_output=True)
        metadata = json.loads(base.read_text())
    if metadata['version'] != '0.5.1' or metadata['settings'].get('creatures'):
        parser.error('unexpected base; review category composition first')
    rules = json.loads((ROOT / 'config/newHorizonsCreatureCategories.json').read_text())
    texts = json.loads((ROOT / 'config/newHorizonsCreatureCategoryTexts.json').read_text())
    if metadata['translations'].keys() & texts.keys():
        parser.error('category text IDs collide with existing translations')
    metadata['version'] = '0.6.0'
    metadata['settings']['creatures'] = {'newHorizonsCategories': rules}
    metadata['translations'].update(texts)
    metadata['description'] += (
        ' Private partial-Conflux creature classification preview: Pixies/Sprites Core,'
        ' five elemental pairs Elite, Phoenix and provisionally Firebird Champion.'
        ' Existing recruitment, upgrades, statistics, capacity and AI values are unchanged;'
        ' other creatures are deliberately unmapped. Not a complete roster redesign.')
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
