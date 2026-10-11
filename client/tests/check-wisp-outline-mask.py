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
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
from nhart_test_resources import ARCHIVE, ArtPath


def outline_threshold(descriptor):
    """Require the solid-mask binding in every actual shipped animation group."""
    sequences = descriptor.get('sequences', [])
    if len(sequences) != 32:
        raise ValueError('Expected complete 32-group Wisp descriptor')
    for sequence in sequences:
        if (sequence.get('generateOverlay') != 1
                or type(sequence.get('overlayAlphaThreshold')) is not int
                or sequence['overlayAlphaThreshold'] != 64):
            raise ValueError('Wisp descriptor must bind solid overlay threshold 64')
    return sequences[0]['overlayAlphaThreshold']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--mod', type=Path, default=ROOT / 'Mods/new-horizons',
                        help='Committed mod location (frames and descriptors are read from its verified NHART)')
    args = parser.parse_args()
    if args.mod.resolve() != (ROOT / 'Mods/new-horizons').resolve():
        parser.error('This shipping-resource check requires the committed mod; no loose-image fallback is supported')
    original_archive_hash = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
    checked = 0
    for name in ('Wisp', 'WispUpgrade'):
        descriptor = json.loads((ArtPath() / f'{name}.json').read_text())
        threshold = outline_threshold(descriptor)
        frames = sorted({frame for group in descriptor['sequences'] for frame in group['frames']})
        for frame in frames:
            path = ArtPath() / descriptor['basepath'] / frame
            original_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            image = path.open_image().convert('RGBA')
            alpha = image.getchannel('A')
            output = subprocess.run(
                [str(args.driver.resolve()), str(image.width), str(image.height), str(threshold)],
                input=alpha.tobytes(), stdout=subprocess.PIPE, check=True).stdout
            assert len(output) == image.width * image.height
            assert set(output) <= {0, 255}, f'Faint mask values: {path}'
            if alpha.getextrema()[1] >= threshold:
                assert 255 in output, f'Empty silhouette: {path}'
            else:
                assert not any(output), f'Outline survives a faded-out creature: {path}'
            # A silhouette cannot paint the whole image canvas border.
            assert output[0] == output[image.width - 1] == output[-1] == output[-image.width] == 0
            for source_alpha, outline_alpha in zip(alpha.tobytes(), output):
                assert not (source_alpha >= threshold and outline_alpha), f'Outline overwrites mask interior: {path}'
            assert hashlib.sha256(path.read_bytes()).hexdigest() == original_hash
            checked += 1
    # Resource reads are cached; independently re-read the archive to prove
    # that the driver did not modify the shipped inputs during this run.
    assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == original_archive_hash
    print(f'Wisp solid silhouette mask passes for {checked} distinct authored frames; input bytes unchanged.')


if __name__ == '__main__':
    main()
