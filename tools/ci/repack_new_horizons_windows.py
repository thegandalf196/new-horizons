#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Repair notices/source coverage without changing previously built Windows payloads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def mutable(name):
    return name.startswith('licenses/dependencies/') or name in {
        'DEPENDENCIES.json', 'BUILD-IDENTITY.json', 'SOURCE-NOTICE.txt',
        'SHA256SUMS.txt', 'REPACK-PROVENANCE.json',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-dir', type=Path, required=True)
    parser.add_argument('--original-run-id', required=True)
    parser.add_argument('--preflight-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--local-run-id', help='Explicit local evidence identity; never a GitHub run claim')
    args = parser.parse_args()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if args.local_run_id:
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            raise RuntimeError('Local packaging mode is not a GitHub Actions run')
        packaging_run_id = 'local:' + args.local_run_id
        execution = 'local'
    else:
        if revision != os.environ['GITHUB_SHA']:
            raise RuntimeError('Packaging checkout identity mismatch')
        packaging_run_id = os.environ['GITHUB_RUN_ID']
        execution = 'github-actions'
    if subprocess.check_output(['git', 'diff', 'HEAD', '--',
            'tools/ci/package_new_horizons_windows.py',
            'tools/ci/repack_new_horizons_windows.py',
            'tools/ci/prepare_frozen_windows_notice_overlay.py']):
        raise RuntimeError('Packaging tools must match their committed source identity')
    archives = list(args.original_dir.rglob('New-Horizons-Windows-x64-*.zip'))
    if len(archives) != 1:
        raise RuntimeError('Expected exactly one original playable ZIP')
    original = archives[0]
    parent = original.parent
    for line in (parent / 'SHA256SUMS.txt').read_text().splitlines():
        expected, name = line.split('  ', 1)
        if Path(name).name != name or digest(parent / name) != expected:
            raise RuntimeError('Original archive checksum/name mismatch')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='nh-license-repack-') as temporary:
        stage = Path(temporary)
        with zipfile.ZipFile(original) as archive:
            members = archive.infolist()
            names = [entry.filename for entry in members]
            if len(set(name.casefold() for name in names)) != len(names):
                raise RuntimeError('Duplicate original ZIP members')
            if sum(entry.file_size for entry in members) > 200 * 1024 * 1024:
                raise RuntimeError('Unexpected original ZIP expansion size')
            roots = set()
            for entry in members:
                name = entry.filename
                if name.startswith('/') or '\\' in name or ':' in name or '..' in name.split('/'):
                    raise RuntimeError('Unsafe original ZIP path')
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise RuntimeError('Unexpected original ZIP symlink')
                roots.add(name.split('/')[0])
            if len(roots) != 1:
                raise RuntimeError('Expected one original package root')
            archive.extractall(stage)
        package = stage / roots.pop()
        for line in (package / 'SHA256SUMS.txt').read_text().splitlines():
            expected, name = line.split('  ', 1)
            if name.startswith('/') or '\\' in name or ':' in name or '..' in name.split('/'):
                raise RuntimeError('Unsafe original payload manifest path')
            if digest(package / name) != expected:
                raise RuntimeError('Original payload manifest mismatch: ' + name)
        identity = json.loads((package / 'BUILD-IDENTITY.json').read_text())
        if str(identity['run_id']) != args.original_run_id:
            raise RuntimeError('Original compiled run identity mismatch')
        protected = {p.relative_to(package).as_posix(): digest(p)
                     for p in package.rglob('*') if p.is_file() and not mutable(p.relative_to(package).as_posix())}
        source_name = identity['source_archive']
        dependency_name = identity['dependency_source_archive']
        for name in (source_name, dependency_name):
            if Path(name).name != name:
                raise RuntimeError('Unsafe source archive name')
        if digest(parent / source_name) != identity['source_archive_sha256']:
            raise RuntimeError('Original corresponding fork source mismatch')
        shutil.copyfile(parent / source_name, args.output_dir / source_name)
        shutil.copyfile(args.preflight_dir / 'dependency-sources.tar.gz', args.output_dir / dependency_name)
        shutil.rmtree(package / 'licenses/dependencies')
        shutil.copytree(args.preflight_dir / 'notices/licenses/dependencies', package / 'licenses/dependencies')
        shutil.copyfile(args.preflight_dir / 'notices/DEPENDENCIES.json', package / 'DEPENDENCIES.json')
        dependencies = json.loads((package / 'DEPENDENCIES.json').read_text())
        covered = {entry['reference'] for entry in dependencies}
        media = json.loads((package / 'MEDIA-RUNTIME.json').read_text())
        required = {entry['reference'] for entry in media['dependencies'] if entry.get('reference')}
        if required - covered:
            raise RuntimeError('Missing media dependency coverage: ' + repr(required - covered))
        for entry in dependencies:
            if not entry.get('system_only') and (not entry.get('notices') or not entry.get('source_archive_directory')):
                raise RuntimeError('Incomplete dependency notice/source coverage: ' + entry['reference'])
            if any(not name.startswith('licenses/dependencies/') or '..' in name.split('/') or not (package / name).is_file() for name in entry['notices']):
                raise RuntimeError('Dependency notice file missing')
        packaging_source = 'New-Horizons-Packaging-' + revision[:12] + '-source.tar.gz'
        essential = ['tools/ci/package_new_horizons_windows.py', 'tools/ci/repack_new_horizons_windows.py']
        if (args.preflight_dir / 'NOTICE-OVERLAY-PROVENANCE.json').is_file():
            essential.append('tools/ci/prepare_frozen_windows_notice_overlay.py')
        with tarfile.open(args.preflight_dir / 'fork-source.tar.gz', 'r:gz') as sources:
            members = sources.getmembers()
            for name in essential:
                matching = [entry for entry in members if entry.name.endswith('/' + name)]
                expected = subprocess.check_output(['git', 'show', revision + ':' + name])
                if (len(matching) != 1 or not matching[0].isfile()
                        or matching[0].size != len(expected)
                        or sources.extractfile(matching[0]).read() != expected):
                    raise RuntimeError('Packaging source archive differs from committed tool: ' + name)
        shutil.copyfile(args.preflight_dir / 'fork-source.tar.gz', args.output_dir / packaging_source)
        identity.update(dependency_source_sha256=digest(args.output_dir / dependency_name),
                        packaging_revision=revision, packaging_run_id=packaging_run_id,
                        packaging_execution=execution,
                        packaging_source_archive=packaging_source,
                        packaging_source_sha256=digest(args.output_dir / packaging_source))
        (package / 'BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        with (package / 'SOURCE-NOTICE.txt').open('a', encoding='utf-8') as stream:
            stream.write('\nNotice/source-only packaging repair: compiled source and original build run remain those in BUILD-IDENTITY.json.\n'
                         'All executable, DLL, engine-resource and launcher bytes are unchanged.\n'
                         'Exact updated packaging tools (not the compiled engine revision): ' + packaging_source + '\n')
        after = {p.relative_to(package).as_posix(): digest(p)
                 for p in package.rglob('*') if p.is_file() and not mutable(p.relative_to(package).as_posix())}
        if after != protected:
            raise RuntimeError('Repack changed executable/runtime/resource/launcher payload')
        overlay_file = args.preflight_dir / 'NOTICE-OVERLAY-PROVENANCE.json'
        overlay = json.loads(overlay_file.read_text()) if overlay_file.is_file() else None
        if overlay is not None and (overlay['original_zip_sha256'] != digest(original)
                or overlay['dependency_source_sha256'] != digest(args.output_dir / dependency_name)):
            raise RuntimeError('Notice overlay provenance does not match supplied archives')
        (package / 'REPACK-PROVENANCE.json').write_text(json.dumps({
            'original_zip_sha256': digest(original), 'original_build_run': args.original_run_id,
            'packaging_revision': revision, 'packaging_run_id': packaging_run_id,
            'packaging_execution': execution, 'notice_overlay': overlay,
            'unchanged_payload_sha256': protected,
            'scope': 'Dependency notices and corresponding sources only; no recompile or gameplay execution',
        }, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        files = sorted(p for p in package.rglob('*') if p.is_file() and p.name != 'SHA256SUMS.txt')
        (package / 'SHA256SUMS.txt').write_text(''.join(digest(p) + '  ' + p.relative_to(package).as_posix() + '\n' for p in files), encoding='utf-8')
        with zipfile.ZipFile(args.output_dir / original.name, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(package.rglob('*')):
                if path.is_file():
                    archive.write(path, package.name + '/' + path.relative_to(package).as_posix())
    (args.output_dir / 'SHA256SUMS.txt').write_text(''.join(digest(p) + '  ' + p.name + '\n'
        for p in sorted(args.output_dir.iterdir()) if p.is_file()), encoding='utf-8')
    print('Notice/source repair complete; all original compiled/runtime/resource/launcher bytes verified unchanged')


if __name__ == '__main__':
    main()
