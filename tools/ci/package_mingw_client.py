#!/usr/bin/env python3
"""Package the bounded, already-compiled local MinGW increment, not an MSVC build.

Consumes execution identities and pre-collected source/notices. No compilation,
Conan dependency build, worktree checkout or purchaser data ingestion occurs here.
"""
import argparse
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import zipfile

import package_new_horizons_windows as common
from collect_mingw_sources import CATALOG, source_entries, validate_runtime_provenance
from git_source_snapshot import git, source_snapshot
from mingw_runtime import audit_directory, stage_gnu_runtime, stage_ogg_loader_alias

HELPERS = ('Play-New-Horizons.cmd', 'Start-New-Horizons.ps1', 'README-New-Horizons.txt', 'dirs.json')
RESOURCES = ('config', 'scripts', 'Mods/vcmi', 'Mods/new-horizons')


def committed_file(root, revision, name):
    return git(root, 'show', revision + ':' + name)


def verify_bundle(bundle):
    manifest = json.loads((bundle / 'BUNDLE-IDENTITY.json').read_text())
    inventory = manifest['files']
    actual = {p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file() and p != bundle / 'BUNDLE-IDENTITY.json'}
    if not inventory or actual != set(inventory) or any(p.is_symlink() for p in bundle.rglob('*')):
        raise RuntimeError('Source bundle inventory/link mismatch')
    for name, digest in inventory.items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name:
            raise RuntimeError('Unsafe source bundle path')
        path = bundle / name
        if path.is_symlink() or common.sha256(path) != digest:
            raise RuntimeError('Changed source/notices bundle: ' + name)
    return manifest


def stage_committed_resources(root, revision, destination):
    data = git(root, 'archive', revision, '--', *RESOURCES)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or '\\' in member.name:
                raise RuntimeError('Unsafe committed resource path')
            if member.isdir():
                continue
            if not member.isfile():
                raise RuntimeError('Linked/nonregular committed resource is not accepted')
            target = destination / member.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())
    for name in RESOURCES:
        if not (destination / name).is_dir():
            raise RuntimeError('Required curated resource tree missing: ' + name)


def stage_gnu_notices(package):
    destination = package / 'licenses/GNU'
    destination.mkdir(parents=True)
    text = ''
    for name in ('gcc-mingw-w64-x86-64-posix-runtime', 'mingw-w64-x86-64-dev'):
        original = Path('/usr/share/doc') / name / 'copyright'
        data = original.read_bytes()
        (destination / (name + '-copyright.txt')).write_bytes(data)
        text += data.decode('utf-8')
    common_licenses = destination / 'common-licenses'
    common_licenses.mkdir()
    for original in Path('/usr/share/common-licenses').iterdir():
        if original.is_file() and str(original) in text:
            shutil.copyfile(original, common_licenses / original.name)
    if not (common_licenses / 'GPL-3').is_file() or 'GCC RUNTIME LIBRARY EXCEPTION' not in text:
        raise RuntimeError('Incomplete GNU runtime license/exception coverage')
    (destination / 'README.txt').write_text(
        'Original complete distro copyright inventories are preserved.\n'
        'Referenced /usr/share/common-licenses texts are copied into common-licenses/.\n'
        'These inventories include components outside the selected runtime DLLs.\n'
        'Exact GCC, MinGW wrapper and MinGW-w64 source packages accompany this ZIP.\n')


def verify_gnu_sources(directory, runtimes):
    validate_runtime_provenance(runtimes)
    document = json.loads((directory / 'GNU-RUNTIME-SOURCES.json').read_text())
    recorded = {x['file']: (x['sha256'], x['distribution_package']) for x in document['runtimes']}
    if recorded != {x['file']: (x['sha256'], x['distribution_package']) for x in runtimes}:
        raise RuntimeError('GNU source provenance does not match staged runtimes')
    files = [directory / 'GNU-RUNTIME-SOURCES.json']
    for source, version, _, digest in CATALOG:
        descriptor = directory / f'{source}_{version}.dsc'
        if descriptor.is_symlink() or common.sha256(descriptor) != digest:
            raise RuntimeError('GNU descriptor changed')
        files.append(descriptor)
        for entry in source_entries(descriptor.read_bytes(), source, version):
            path = directory / entry['file']
            if path.is_symlink() or path.stat().st_size != entry['size'] or common.sha256(path) != entry['sha256']:
                raise RuntimeError('GNU corresponding source archive changed: ' + path.name)
            files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('install-dir', 'build-dir', 'build-identity', 'conan-graph', 'dependency-bundle',
                 'gnu-source-dir', 'engine-source-archive', 'submodule-cache', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--packaging-commit', required=True)
    args = parser.parse_args()
    root = Path(common.run('git', 'rev-parse', '--show-toplevel'))
    packaging_commit = git(root, 'rev-parse', '--verify', '--end-of-options', args.packaging_commit + '^{commit}').decode().strip()
    for name in ('package_mingw_client.py', 'package_new_horizons_windows.py', 'mingw_runtime.py',
                 'collect_mingw_sources.py', 'git_source_snapshot.py'):
        if (Path(__file__).parent / name).read_bytes() != committed_file(root, packaging_commit, 'tools/ci/' + name):
            raise RuntimeError('Executing packaging code is not its declared committed source: ' + name)
    build = json.loads(args.build_identity.read_text())
    revision = build['source_commit']
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision) or build['build_install_exit'] != 0:
        raise RuntimeError('Missing successful compiled identity')
    for name, digest in build['binaries'].items():
        if name not in {'VCMI_client.exe', 'VCMI_lib.dll'} or common.sha256(args.install_dir / name) != digest:
            raise RuntimeError('Compiled artifact differs from build identity')
    if set(build['binaries']) != {'VCMI_client.exe', 'VCMI_lib.dll'}:
        raise RuntimeError('Incomplete compiled binary identity')
    if revision.encode() not in (args.install_dir / 'VCMI_lib.dll').read_bytes():
        raise RuntimeError('Facade does not embed declared source revision')
    bundle = verify_bundle(args.dependency_bundle)
    if bundle['conan_graph_sha256'] != common.sha256(args.conan_graph):
        raise RuntimeError('Dependency bundle does not match exact compiler graph')
    if common.sha256(args.engine_source_archive) != build['source_archive_sha256']:
        raise RuntimeError('Corresponding engine source changed')
    if args.output_dir.exists():
        raise RuntimeError('Output exists; prior artifacts are immutable')
    args.output_dir.mkdir(parents=True)
    name = 'New-Horizons-Windows-x64-' + revision[:12]
    package = args.output_dir / name
    package.mkdir()
    for path in sorted(args.install_dir.iterdir()):
        if path.is_file() and path.suffix.lower() in {'.exe', '.dll'}:
            if path.is_symlink() or (path.suffix.lower() == '.exe' and path.name != 'VCMI_client.exe'):
                raise RuntimeError('Unexpected installed executable/link')
            shutil.copyfile(path, package / path.name)
    stage_committed_resources(root, revision, package)
    for helper in HELPERS:
        target = package / ('config/dirs.json' if helper == 'dirs.json' else helper)
        target.write_bytes(committed_file(root, packaging_commit, 'tools/windows/' + helper))
    for filename in ('license.txt', 'AUTHORS.h'):
        (package / filename).write_bytes(committed_file(root, revision, filename))
    embedded = package / 'licenses/xBRZ'
    embedded.mkdir(parents=True)
    (embedded / 'License.txt').write_bytes(committed_file(root, revision, 'client/xBRZ/License.txt'))
    (embedded / 'ATTRIBUTION.txt').write_bytes(committed_file(root, revision, 'client/xBRZ/xbrz.cpp').split(b'#include "xbrz.h"', 1)[0])
    runtimes = stage_gnu_runtime(package)
    alias = stage_ogg_loader_alias(package)
    images = audit_directory(package)
    required_media, media = common.media_runtime_roots(args.conan_graph, allow_mingw_import_archive_links=True)
    if not required_media <= {p.name.lower() for p in package.iterdir()}:
        raise RuntimeError('Missing conservative dynamic-media DLL closure')
    common.write_json(package / 'PE-IMPORTS.json', images)
    common.write_json(package / 'GNU-RUNTIME.json', {'runtimes': runtimes, 'loader_alias': alias})
    common.write_json(package / 'MEDIA-RUNTIME.json', {'dependencies': media, 'scope': 'Conservative graph retention, not Windows media playback acceptance'})
    shutil.copytree(args.dependency_bundle / 'licenses/dependencies', package / 'licenses/dependencies')
    shutil.copyfile(args.dependency_bundle / 'DEPENDENCIES.json', package / 'DEPENDENCIES.json')
    stage_gnu_notices(package)
    source_files = verify_gnu_sources(args.gnu_source_dir, runtimes)
    gnu_name = name + '-gnu-runtime-sources.tar.gz'
    with tarfile.open(args.output_dir / gnu_name, 'w:gz') as archive:
        for path in source_files:
            archive.add(path, arcname='gnu-runtime-sources/' + path.name, recursive=False)
    companions = {'engine_source': (args.engine_source_archive, name + '-source.tar.gz'),
                  'dependency_sources': (args.dependency_bundle / 'dependency-sources.tar.gz', name + '-dependency-sources.tar.gz')}
    identities = {}
    for key, (source, filename) in companions.items():
        shutil.copyfile(source, args.output_dir / filename)
        identities[key] = {'file': filename, 'sha256': common.sha256(args.output_dir / filename)}
    packaging_name = name + '-packaging-source-' + packaging_commit[:12] + '.tar.gz'
    packaging_source = source_snapshot(root, packaging_commit, args.output_dir / packaging_name, args.submodule_cache)
    identities['packaging_source'] = {'file': packaging_name, 'sha256': common.sha256(args.output_dir / packaging_name), **packaging_source}
    identities['gnu_runtime_sources'] = {'file': gnu_name, 'sha256': common.sha256(args.output_dir / gnu_name)}
    provenance = common.build_provenance(args.build_dir)
    provenance['compiler_versions'] = provenance.pop('msvc_compiler_versions', [])
    if provenance['compiler_versions'] != ['13.2.0'] or provenance['cache_options'].get('CMAKE_BUILD_TYPE') != 'Release':
        raise RuntimeError('Compiler/configuration differs from the audited local lane')
    common.write_json(package / 'BUILD-IDENTITY.json', {
        'project': 'Heroes III: New Horizons', 'source_commit': revision,
        'packaging_source_commit': packaging_commit, 'source_companions': identities,
        'source_repository': 'https://github.com/thegandalf196/vcmi',
        'compiler': 'Local MinGW-w64 GNU C++13 POSIX/SEH; NOT MSVC or Windows82 cache',
        'build_provenance': provenance, 'execution_identity': build,
        'platform': 'Windows x64', 'configuration': 'Release', 'render_backend': 'SDL3',
        'transport': 'authoritative in-process simulation', 'engine_resource_scope': list(RESOURCES),
        'acceptance': 'Incremental commands/schools preview; static PE closure is not native Windows gameplay acceptance',
        'proprietary_assets_included': False, 'signed': False,
    })
    (package / 'SOURCE-NOTICE.txt').write_text(
        'VCMI-derived GPL-covered fork. Preserve license.txt, AUTHORS.h and licenses/.\n'
        'BUILD-IDENTITY.json identifies exact engine and packaging commits and all accompanying source archives/hashes.\n'
        'Conan implementations, recipes and patches: dependency-sources companion and DEPENDENCIES.json.\n'
        'GNU runtimes: complete exact distro/upstream sources, original signed descriptors and licenses/GNU.\n'
        'GCC Runtime Library Exception is included; embedded xBRZ GPLv3 terms are in licenses/xBRZ.\n'
        'No MSVC redistribution claim: this is a separate local MinGW build, not the repaired old Windows82 executable.\n'
        'Original Heroes III Complete assets are required, external and not redistributed.\n'
        'Unsigned incremental preview; full redesign and native Windows gameplay acceptance remain separate gates.\n')
    for path in package.rglob('*'):
        if path.is_symlink() or (path.is_file() and path.suffix.lower() in common.FORBIDDEN_ASSETS):
            raise RuntimeError('Forbidden linked/asset/save payload: ' + path.relative_to(package).as_posix())
    (package / 'SHA256SUMS.txt').write_text('\n'.join(
        f'{common.sha256(p)}  {p.relative_to(package).as_posix()}' for p in sorted(package.rglob('*')) if p.is_file()) + '\n')
    with zipfile.ZipFile(args.output_dir / (name + '.zip'), 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                archive.write(path, name + '/' + path.relative_to(package).as_posix())
    (args.output_dir / 'SHA256SUMS.txt').write_text('\n'.join(
        f'{common.sha256(p)}  {p.name}' for p in sorted(args.output_dir.iterdir()) if p.is_file()) + '\n')
    print('PACKAGED (independent actual archive/runtime audit still required): ' + name)


if __name__ == '__main__':
    main()
