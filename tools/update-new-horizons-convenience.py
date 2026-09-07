#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate an isolated presentation preview; never activate the live module."""
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
    try:
        output.relative_to((ROOT / 'build').resolve())
    except ValueError:
        parser.error('preview output must remain under build/')
    if args.output.is_symlink() or (output.exists() and not args.check):
        parser.error('refusing existing output or symlink')
    with tempfile.TemporaryDirectory(prefix='nh-convenience-', dir=ROOT / 'build') as temporary:
        base = Path(temporary) / 'mod.json'
        subprocess.run([sys.executable, str(ROOT / 'tools/update-new-horizons-module.py'),
                        '--mastery-preview-output', str(base)], check=True, capture_output=True)
        metadata = json.loads(base.read_text())
    if metadata['version'] != '0.5.0' or metadata.get('bonuses'):
        parser.error('unexpected base preview; review presentation composition first')
    metadata['version'] = '0.5.1'
    metadata['bonuses'] = json.loads((ROOT / 'config/newHorizonsConvenienceBonuses.json').read_text())
    metadata['filesystem'][''] = [{'type': 'dir', 'path': '/Content'}]
    metadata['description'] += ' Includes independently authored quick-save/load buttons and creature ability icons; landscape presentation only.'
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
