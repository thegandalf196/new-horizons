#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract verified selected numerical masks into ephemeral CI test inputs."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nhart


def prepare(pack, manifest, output):
    verified = nhart.verify(pack, manifest)
    masks = [row for row in verified['entries']
             if row['resource'].startswith('SPRITES/NH_academy_')
             and row['resource'].endswith('_portrait_mask.png')]
    if len(masks) != 12:
        raise RuntimeError('Expected exactly 12 selected portrait masks')
    payloads = []
    with Path(pack).open('rb') as stream:
        for row in masks:
            stream.seek(row['offset'])
            data = stream.read(row['size'])
            if hashlib.sha256(data).hexdigest() != row['sha256']:
                raise RuntimeError('Selected mask changed after verification')
            if (len(data) < 33 or data[:16] != b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR'
                    or struct.unpack('>II', data[16:24]) != (58, 64)
                    or data[24:28] != bytes([8, 0, 0, 0])):
                raise RuntimeError('Selected mask is not native 58x64 grayscale8 PNG')
            payloads.append((Path(row['resource']).name, data))
    # Validate all inputs before writing any fixture bytes.
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    for name, data in payloads:
        (output / name).write_bytes(data)
    print('Prepared 12 hash-verified selected masks; no original/private art inputs')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.pack, args.manifest, args.output)
