#!/usr/bin/env python3
"""Archive exact committed source/submodule blobs without changing shared worktrees."""
import argparse
import gzip
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile

from package_new_horizons_windows import validate_archive_link


def git(root, *arguments):
    return subprocess.check_output(['git', '-C', str(root), *arguments], stderr=subprocess.PIPE)


def source_snapshot(root, revision, output, submodule_cache=None):
    root = root.resolve(strict=True)
    revision = git(root, 'rev-parse', '--verify', '--end-of-options', revision + '^{commit}').decode().strip()
    if output.exists() or output.is_symlink():
        raise RuntimeError('Source output already exists; refusing replacement')
    archive_root = 'new-horizons-' + revision
    excluded, count = [], 0

    def append(repository, commit, relative, archive):
        nonlocal count
        try:
            actual_root = Path(git(repository, 'rev-parse', '--show-toplevel').decode().strip()).resolve()
            initialized = actual_root == repository.resolve()
        except subprocess.CalledProcessError:
            initialized = False
        if not initialized:
            cached = submodule_cache / (commit + '.git') if submodule_cache is not None else None
            if cached is None or not cached.is_dir() or git(cached, 'rev-parse', '--is-bare-repository').strip() != b'true':
                raise RuntimeError('Uninitialized submodule (provide pinned bare source cache): ' + relative)
            repository = cached
        entries = git(repository, 'ls-tree', '-rz', commit).split(b'\0')
        with subprocess.Popen(['git', '-C', str(repository), 'cat-file', '--batch'],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE) as objects:
            for entry in filter(None, entries):
                metadata, name = entry.split(b'\t', 1)
                mode, kind, identifier = metadata.split()
                name = name.decode('utf-8')
                full_name = relative + name
                if full_name == 'CI/deploy_rsa.enc' or full_name.startswith('docs/NH_'):
                    excluded.append(full_name)
                    continue
                if kind == b'commit':
                    append(repository / name, identifier.decode(), full_name + '/', archive)
                    continue
                if kind != b'blob':
                    raise RuntimeError('Unexpected Git source type: ' + full_name)
                objects.stdin.write(identifier + b'\n')
                objects.stdin.flush()
                header = objects.stdout.readline().split()
                if len(header) != 3 or header[0] != identifier or header[1] != b'blob':
                    raise RuntimeError('Unreadable committed blob: ' + full_name)
                size = int(header[2])
                data = objects.stdout.read(size)
                if len(data) != size or objects.stdout.read(1) != b'\n':
                    raise RuntimeError('Truncated committed blob: ' + full_name)
                info = tarfile.TarInfo(archive_root + '/' + full_name)
                info.mode = int(mode, 8) & 0o777
                if mode == b'120000':
                    info.type = tarfile.SYMTYPE
                    info.linkname = data.decode('utf-8')
                    validate_archive_link(info, archive_root)
                    archive.addfile(info)
                else:
                    info.size = size
                    archive.addfile(info, io.BytesIO(data))
                count += 1
            objects.stdin.close()
            if objects.wait() != 0:
                raise RuntimeError('Git blob reader failed')

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.nh-source-') as temporary:
        with gzip.GzipFile(fileobj=temporary, mode='wb', filename='', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.PAX_FORMAT) as archive:
                append(root, revision, '', archive)
        temporary.flush()
        # Exclusive destination creation never replaces a prior archive.
        temporary.seek(0)
        with output.open('xb') as destination:
            import shutil
            shutil.copyfileobj(temporary, destination)
    return {'source_commit': revision, 'source_entries': count, 'excluded': sorted(excluded)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('.'))
    parser.add_argument('--revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--submodule-cache', type=Path, help='Existing bare object caches named <pinned-commit>.git; no fetch or checkout is performed')
    args = parser.parse_args()
    print(source_snapshot(args.root, args.revision, args.output, args.submodule_cache))


if __name__ == '__main__':
    main()
