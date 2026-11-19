#!/usr/bin/env python3
"""Archive a verified, updater-capable New Horizons Linux x64 player stage."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import gzip
import hashlib
from pathlib import Path
import re
import sys
import tarfile

from stage_linux_client import verify_candidate

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from PortableReleaseUpdate import PACKAGE_ROOT, verify_player_files


def public_mode(info):
    info.uid = info.gid = info.mtime = 0
    info.uname = info.gname = ''
    info.mode = 0o755 if info.isdir() or info.mode & 0o111 else 0o644
    return info


def archive_stage(stage, output):
    if stage.is_symlink() or stage.name != PACKAGE_ROOT:
        raise ValueError('Expected an ordinary Linux player stage')
    if output.resolve().is_relative_to(stage.resolve()):
        raise ValueError('The archive destination must be outside its input stage')
    identity = verify_candidate(stage)
    revision = identity['source_commit']
    if not isinstance(revision, str) or not re.fullmatch(r'[a-f0-9]{40}', revision):
        raise ValueError('Expected the full staged source revision')
    verify_player_files(stage)
    target = output / f'{PACKAGE_ROOT}-{revision[:12]}.tar.gz'
    with target.open('xb') as stream:
        try:
            with gzip.GzipFile(fileobj=stream, filename='', mode='wb', mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode='w|') as archive:
                    archive.add(stage, arcname=PACKAGE_ROOT, filter=public_mode)
        except BaseException:
            target.unlink()
            raise
    with target.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    return target, checksum


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(argv)
    path, checksum = archive_stage(args.stage, args.output_dir)
    print(f'{checksum}  {path.name}')


if __name__ == '__main__':
    main()
