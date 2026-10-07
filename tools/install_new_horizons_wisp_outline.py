#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage a private descriptor-only Wisp outline correction, without roster edits."""
import argparse
import hashlib
import json
from pathlib import Path


def stage(source: Path, output: Path):
    source, output = source.resolve(), output.resolve()
    if output == source or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Source and output must be separate')
    if output.exists():
        raise FileExistsError(output)
    payloads, inputs = {}, {}
    for name in ('Wisp', 'WispUpgrade'):
        relative = Path(f'Mods/new-horizons/Content/sprites/{name}.json')
        path = source / relative
        data = path.read_bytes()
        descriptor = json.loads(data)
        if len(descriptor.get('sequences', [])) != 32:
            raise ValueError('Expected complete 32-group Wisp handoff')
        for sequence in descriptor['sequences']:
            if sequence.get('generateOverlay') != 1:
                raise ValueError('Expected existing silhouette overlay binding')
            sequence['overlayAlphaThreshold'] = 64
        payloads[relative] = (json.dumps(descriptor, indent=2) + '\n').encode()
        inputs[path] = hashlib.sha256(data).hexdigest()
    for path, digest in inputs.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError('Input changed')
    for relative, data in payloads.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    stage(args.source, args.output)
