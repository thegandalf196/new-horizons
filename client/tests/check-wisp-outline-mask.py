#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production outline mask against authored Wisp alpha channels.

Pass a compiled OutlineMaskTest executable; this fixture performs no GUI run,
resource installation, source-alpha modification or engine build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--mod', type=Path, default=Path(__file__).resolve().parents[2] / 'Mods/new-horizons')
    args = parser.parse_args()
    checked = 0
    for name in ('Wisp', 'WispUpgrade'):
        descriptor = json.loads((args.mod / f'Content/sprites/{name}.json').read_text())
        frames = sorted({frame for group in descriptor['sequences'] for frame in group['frames']})
        for frame in frames:
            path = args.mod / 'Images' / descriptor['basepath'] / frame
            original_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            image = Image.open(path).convert('RGBA')
            alpha = image.getchannel('A')
            output = subprocess.run(
                [str(args.driver.resolve()), str(image.width), str(image.height), '64'],
                input=alpha.tobytes(), stdout=subprocess.PIPE, check=True).stdout
            assert len(output) == image.width * image.height
            assert set(output) <= {0, 255}, f'Faint mask values: {path}'
            if alpha.getextrema()[1] >= 64:
                assert 255 in output, f'Empty silhouette: {path}'
            else:
                assert not any(output), f'Outline survives a faded-out creature: {path}'
            # A silhouette cannot paint the whole image canvas border.
            assert output[0] == output[image.width - 1] == output[-1] == output[-image.width] == 0
            for source_alpha, outline_alpha in zip(alpha.tobytes(), output):
                assert not (source_alpha >= 64 and outline_alpha), f'Outline overwrites mask interior: {path}'
            assert hashlib.sha256(path.read_bytes()).hexdigest() == original_hash
            checked += 1
    print(f'Wisp solid silhouette mask passes for {checked} distinct authored frames; input bytes unchanged.')


if __name__ == '__main__':
    main()
