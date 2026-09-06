#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare the audited Windows82 missing-notice overlay without rebuilding payloads.

Preserve every original dependency source entry. Add exact static dependency
sources/notices recovered by collect_notices, checking their identities against
the original immutable Conan cache and parent packages. Copy FFmpeg's full LGPL
from its original source companion. This is not a general dependency auditor.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import zipfile

from package_new_horizons_windows import validate_archive_link
from repack_new_horizons_windows import digest

SUPPLEMENTS = {
    'dav1d/1.5.4#001b758cfd88fd816b286e09e686dd5c',
    'plutovg/1.3.3#95e0e6c6c8525ffc0652c4d1198811ae',
    'brotli/1.1.0#3f631ef77008f7b5eb388780116371a3',
}


def safe_name(name):
    if (not name or name.startswith('/') or '\\' in name or ':' in name or '\x00' in name
            or any(part in {'', '.', '..'} for part in name.rstrip('/').split('/'))):
        raise RuntimeError('Unsafe archive path: ' + name)
    return PurePosixPath(name)


def prepare(original_dir, supplement, supplement_source, prebuilt_cache, output):
    archives = list(original_dir.rglob('New-Horizons-Windows-x64-*.zip'))
    if len(archives) != 1:
        raise RuntimeError('Expected one original playable ZIP')
    original = archives[0]
    with zipfile.ZipFile(original) as archive:
        names = archive.namelist()
        if len({n.casefold() for n in names}) != len(names):
            raise RuntimeError('Duplicate original ZIP members')
        for name in names:
            safe_name(name)
        identities = [n for n in names if n.endswith('/BUILD-IDENTITY.json')]
        if len(identities) != 1:
            raise RuntimeError('Expected one original build identity')
        identity = json.loads(archive.read(identities[0]))
        prefix = identities[0].removesuffix('BUILD-IDENTITY.json')
        dependency_name = identity['dependency_source_archive']
        if safe_name(dependency_name).name != dependency_name:
            raise RuntimeError('Unsafe dependency source archive name')
        source = original.parent / dependency_name
        if digest(source) != identity['dependency_source_sha256']:
            raise RuntimeError('Original dependency source identity mismatch')
        dependencies = json.loads(archive.read(prefix + 'DEPENDENCIES.json'))
        added = json.loads((supplement / 'DEPENDENCIES.json').read_text())
        if len(added) != 3 or {d['reference'] for d in added} != SUPPLEMENTS:
            raise RuntimeError('Expected all three audited static source supplements')
        if any(d['reference'] in SUPPLEMENTS for d in dependencies):
            raise RuntimeError('Original already contains supplements; overlay is not applicable')
        if digest(prebuilt_cache) != identity['prebuilt_dependencies']['sha256']:
            raise RuntimeError('Original prebuilt cache identity mismatch')
        with tarfile.open(prebuilt_cache, 'r:xz') as cached:
            cache = json.load(cached.extractfile('pkglist.json'))
            required = set()
            parent_proofs = []
            dll_matches = []
            for dependency in dependencies:
                if dependency.get('system_only'):
                    continue
                name, revision = dependency['reference'].split('#')
                package = cache[name]['revisions'][revision]['packages'][dependency['package_id']]
                if dependency['package_revision'] not in package['revisions']:
                    raise RuntimeError('Original parent package revision mismatch')
                required.update(package['info'].get('requires', []))
                parent_proofs.append({'reference': dependency['reference'],
                                     'package_id': dependency['package_id'],
                                     'package_revision': dependency['package_revision'],
                                     'requires': package['info'].get('requires', [])})
                if name.startswith(('sdl_ttf/', 'ffmpeg/')):
                    folder = package['revisions'][dependency['package_revision']]['package_folder']
                    for member in cached.getmembers():
                        if (member.isfile() and member.name.startswith(folder + '/bin/')
                                and member.name.lower().endswith('.dll')):
                            leaf = PurePosixPath(member.name).name
                            matches = [n for n in names if PurePosixPath(n).name.casefold() == leaf.casefold()]
                            if len(matches) != 1:
                                raise RuntimeError('Missing/ambiguous original parent DLL')
                            actual = hashlib.sha256(archive.read(matches[0])).hexdigest()
                            if actual != hashlib.file_digest(cached.extractfile(member), 'sha256').hexdigest():
                                raise RuntimeError('Original DLL differs from claimed cached parent')
                            dll_matches.append({'file': leaf, 'sha256': actual})
            if not any(d['file'].casefold() == 'sdl3_ttf.dll' for d in dll_matches):
                raise RuntimeError('No original SDL_ttf binary corroboration')
            if not any(d['file'].casefold() == 'avcodec-63.dll' for d in dll_matches):
                raise RuntimeError('No original FFmpeg binary corroboration')
            missing = {r.split(':', 1)[0] for r in required if '#' in r} - {d['reference'] for d in dependencies}
            if missing != SUPPLEMENTS:
                raise RuntimeError('Unreviewed or missing static dependency closure')
            for dependency in added:
                ref = dependency['reference']
                if ref + ':' + dependency['package_id'] not in required:
                    raise RuntimeError('Supplement is not an exact recorded parent requirement')
                name, revision = ref.split('#')
                package = cache[name]['revisions'][revision]['packages'][dependency['package_id']]
                if (dependency['package_revision'] not in package['revisions']
                        or dependency['settings'] != package['info']['settings']
                        or dependency['options'] != package['info']['options']):
                    raise RuntimeError('Supplement identity differs from original cache')
                if package['info'].get('requires'):
                    raise RuntimeError('Supplement has unreviewed transitive requirements')
                if not dependency.get('notices') or not dependency.get('source_archive_directory'):
                    raise RuntimeError('Supplement lacks source/notices')
        ffmpeg = [d for d in dependencies if d['reference'].startswith('ffmpeg/')]
        if len(ffmpeg) != 1:
            raise RuntimeError('Expected one FFmpeg dependency')
        output.mkdir(parents=True, exist_ok=False)
        notices = output / 'notices'
        for name in names:
            relative = name.removeprefix(prefix)
            if relative.startswith('licenses/dependencies/') and not name.endswith('/'):
                target = notices / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(name))
    for name in [name for dependency in added for name in dependency['notices']]:
        safe_name(name)
        if not name.startswith('licenses/dependencies/'):
            raise RuntimeError('Invalid supplementary notice path')
        candidate = supplement / name
        if (not candidate.is_file() or candidate.is_symlink()
                or not candidate.resolve().is_relative_to(supplement.resolve())):
            raise RuntimeError('Missing/redirected supplementary notice')
        target = notices / name
        if target.exists():
            raise RuntimeError('Supplement overwrites an original notice')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(candidate, target)
    roots = set()
    seen = set()
    counts = []
    licenses = []
    merged = output / 'dependency-sources.tar.gz'
    with tarfile.open(merged, 'w:gz', format=tarfile.PAX_FORMAT) as destination:
        for source_archive, expected_roots in ((source, None),
                (supplement_source, {d['source_archive_directory'] for d in added})):
            count = 0
            with tarfile.open(source_archive, 'r|gz') as archive:
                for entry in archive:
                    path = safe_name(entry.name)
                    if not (entry.isfile() or entry.isdir() or entry.issym() or entry.islnk()):
                        raise RuntimeError('Unsupported source entry type')
                    if entry.name in seen:
                        raise RuntimeError('Duplicate source entry: ' + entry.name)
                    seen.add(entry.name)
                    root = path.parts[0]
                    if expected_roots is not None and root not in expected_roots:
                        raise RuntimeError('Unexpected supplementary source root')
                    validate_archive_link(entry, root)
                    roots.add(root)
                    stream = archive.extractfile(entry) if entry.isfile() else None
                    if (source_archive == source and entry.isfile()
                            and root == ffmpeg[0]['source_archive_directory']
                            and path.name == 'COPYING.LGPLv2.1'):
                        data = stream.read()
                        if (len(data) < 24000 or b'GNU LESSER GENERAL PUBLIC LICENSE' not in data
                                or b'Version 2.1, February 1999' not in data):
                            raise RuntimeError('Incomplete original FFmpeg LGPL text')
                        stream = io.BytesIO(data)
                        licenses.append((path, data))
                    destination.addfile(entry, stream)
                    count += 1
            counts.append(count)
    if len(licenses) != 1 or counts[1] == 0:
        raise RuntimeError('Expected full FFmpeg LGPL and nonempty supplementary source')
    license_path, license_data = licenses[0]
    notice = 'licenses/dependencies/' + license_path.parts[0] + '/source_folder/' + '/'.join(license_path.parts[1:])
    target = notices / notice
    if target.exists() and target.read_bytes() != license_data:
        raise RuntimeError('Conflicting existing FFmpeg LGPL notice')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(license_data)
    ffmpeg[0]['notices'] = sorted(set(ffmpeg[0]['notices'] + [notice]))
    dependencies += added
    for dependency in dependencies:
        if not dependency.get('system_only') and dependency['source_archive_directory'] not in roots:
            raise RuntimeError('Missing declared dependency source root')
    (notices / 'DEPENDENCIES.json').write_text(json.dumps(dependencies, indent=2, sort_keys=True) + '\n')
    (output / 'NOTICE-OVERLAY-PROVENANCE.json').write_text(json.dumps({
        'original_zip_sha256': digest(original), 'original_dependency_source_sha256': digest(source),
        'supplementary_source_sha256': digest(supplement_source),
        'dependency_source_sha256': digest(merged), 'supplementary_references': sorted(SUPPLEMENTS),
        'original_prebuilt_sha256': digest(prebuilt_cache),
        'matched_parent_packages': parent_proofs, 'original_cached_dll_matches': dll_matches,
        'original_source_entries_preserved': counts[0], 'supplementary_source_entries': counts[1],
        'ffmpeg_license_source_member': license_path.as_posix(),
        'scope': 'Known audited omissions only; no source deletion, binary compilation or gameplay execution',
    }, indent=2, sort_keys=True) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-dir', type=Path, required=True)
    parser.add_argument('--supplement-notices', type=Path, required=True)
    parser.add_argument('--supplement-source', type=Path, required=True)
    parser.add_argument('--prebuilt-cache', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.original_dir, args.supplement_notices, args.supplement_source, args.prebuilt_cache, args.output_dir)
    print('Prepared notice overlay; original source entries retained, independent replacement audit still required')


if __name__ == '__main__':
    main()
