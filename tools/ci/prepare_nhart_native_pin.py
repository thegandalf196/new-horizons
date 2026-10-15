#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Emit NUL-separated native probe arguments after exact committed-pack verification."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nhart

PACK = 'Mods/new-horizons/NewHorizons.nhart'
MANIFEST = 'assets/new-horizons/runtime-art-manifest.json'


def committed_bytes(repo, name):
    return subprocess.run(['git', 'show', 'HEAD:' + name], cwd=repo, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verified_pin(repo, installed_pack):
    manifest_bytes = committed_bytes(repo, MANIFEST)
    # Git may legitimately materialize CRLF in a Windows checkout. Compare
    # strict JSON values (including scalar types), not checkout line endings.
    committed_manifest = nhart.read_json(manifest_bytes)
    checkout_manifest = nhart.read_json((repo / MANIFEST).read_bytes())
    canonical = lambda value: json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False)
    if canonical(checkout_manifest) != canonical(committed_manifest):
        raise ValueError('Selected manifest differs from the committed revision')
    expected_hash = hashlib.sha256(committed_bytes(repo, PACK)).hexdigest()
    if sha256(repo / PACK) != expected_hash:
        raise ValueError('Checkout pack differs from the committed revision')
    if sha256(installed_pack) != expected_hash:
        raise ValueError('Installed pack differs from the committed revision')
    verified = nhart.verify(installed_pack, committed_manifest)
    count = len(verified['manifest']['entries'])
    if not 1 <= count < nhart.MAX_ENTRIES or verified['entryCount'] != count + 1:
        raise ValueError('Invalid committed payload count')
    return expected_hash, count


def encoded_arguments(pin):
    expected_hash, count = pin
    if len(expected_hash) != 64 or any(char not in '0123456789abcdef' for char in expected_hash):
        raise ValueError('Invalid expected SHA256')
    if type(count) is not int or not 1 <= count < nhart.MAX_ENTRIES:
        raise ValueError('Invalid expected payload count')
    return b'\0'.join(part.encode('ascii') for part in (
        '--expected-pack-sha256', expected_hash, '--expected-resources', str(count))) + b'\0'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    args = parser.parse_args()
    try:
        encoded = encoded_arguments(verified_pin(args.repo, args.pack))
    except (OSError, subprocess.CalledProcessError):
        parser.exit(1, 'Committed NHART pin preparation failed; no input paths disclosed\n')
    except (ValueError, nhart.NHArtError) as error:
        parser.exit(1, str(error) + '\n')
    sys.stdout.buffer.write(encoded)


if __name__ == '__main__':
    main()
