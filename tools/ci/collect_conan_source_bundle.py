#!/usr/bin/env python3
"""Collect exact Conan source/notices once for reuse by bounded local packaging."""
import argparse
from pathlib import Path

from package_new_horizons_windows import collect_notices, sha256, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--conan-graph', type=Path, required=True)
    parser.add_argument('--cache-storage', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists():
        raise RuntimeError('Refusing to replace prior source collection evidence')
    if not args.cache_storage.is_dir():
        raise RuntimeError('Exact Conan package storage does not exist')
    args.output_dir.mkdir(parents=True)
    collect_notices(args.conan_graph, args.output_dir,
                    args.output_dir / 'dependency-sources.tar.gz', args.cache_storage.resolve())
    files = {}
    for path in sorted(args.output_dir.rglob('*')):
        if path.is_symlink():
            raise RuntimeError('Linked source/notices bundle payload')
        if path.is_file():
            files[path.relative_to(args.output_dir).as_posix()] = sha256(path)
    write_json(args.output_dir / 'BUNDLE-IDENTITY.json', {
        'conan_graph_sha256': sha256(args.conan_graph), 'files': files,
        'scope': 'Exact Conan host recipes, patches, implementation sources and full notices; no compilation or Windows runtime claim',
    })
    print('PASS: complete immutable-input source/notices bundle inventory written')


if __name__ == '__main__':
    main()
