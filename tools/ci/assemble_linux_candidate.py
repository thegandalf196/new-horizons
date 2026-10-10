#!/usr/bin/env python3
"""Assemble a public raw Linux candidate from a normal installed runtime.

No builds, ELF execution, source extraction or artwork generation. Resource byte
correspondence and the embedded revision are checked; compiled-source, dependency,
license and rendered acceptance still require separately reviewed evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_new_horizons_art_install import verify_install
from linux_playable_snapshot import checked_tree_files, REQUIRED_FILES
from binary_privacy import require_clean
from stage_linux_client import verify_candidate


TREES = ('config', 'scripts', 'Mods/vcmi', 'Mods/new-horizons')
NOTICES = {
    'Mods/new-horizons/notices/NHART_FORMAT.md': 'docs/NHART_FORMAT.md',
    'Mods/new-horizons/notices/NHART_DELIVERY.md': 'docs/NHART_DELIVERY.md',
    'Mods/new-horizons/notices/sorcery-art/LICENSE': 'assets/new-horizons/sorcery-art/LICENSE',
}
for family in ('', 'academy', 'magic-assets', 'Mage Guilds', 'creatures/wisp/handoff-v3',
               'creatures/cabir-master', 'creatures/cabir-master/v3', 'creatures/cabir/v3',
               'creatures/magi-palette/v1'):
    NOTICES['Mods/new-horizons/notices/provenance/' + (family + '/' if family else '') + 'README.md'] = (
        'assets/new-horizons/' + (family + '/' if family else '') + 'README.md')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def regular(path):
    if path.is_symlink() or not path.is_file():
        raise RuntimeError('Required regular input missing: ' + path.name)


def source_notice(repository, commit, name):
    return subprocess.check_output(['git', '-C', str(repository), 'show', commit + ':' + name])


def elf(path):
    regular(path)
    with path.open('rb') as stream:
        header = stream.read(20)
    if len(header) != 20 or header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\x3e\x00':
        raise RuntimeError('Expected Linux ELF64 x86-64 binary: ' + path.name)


def assemble(install, engine_source, source_commit, source_repository, manifest, output):
    if not re.fullmatch('[0-9a-f]{40}', source_commit):
        raise RuntimeError('Source commit must be an exact full lowercase SHA')
    regular(engine_source)
    regular(manifest)
    if install.is_symlink() or not install.is_dir() or any(p.is_symlink() for p in install.rglob('*')):
        raise RuntimeError('Install root must exist without symlinks')
    if output.exists() or output.is_symlink():
        raise RuntimeError('Refusing existing output')
    destination = output.resolve()
    for protected in (install, engine_source, source_repository, manifest):
        resolved = protected.resolve()
        if destination == resolved or destination.is_relative_to(resolved) or resolved.is_relative_to(destination):
            raise RuntimeError('Output overlaps protected input')
    resources = install / 'share/vcmi'
    art = verify_install(resources, manifest)
    client, library = install / 'bin/new-horizons', install / 'lib/libvcmi.so'
    if (install / 'bin/vcmiclient').exists():
        raise RuntimeError('Ambiguous or obsolete installed Linux client')
    elf(client)
    elf(library)
    if not os.access(client, os.X_OK):
        raise RuntimeError('Client is not executable')
    if source_commit.encode() not in library.read_bytes():
        raise RuntimeError('Facade does not contain supplied source revision')
    files = {'new-horizons': client, 'libvcmi.so': library}
    for tree in TREES:
        files.update(checked_tree_files(resources / tree, tree))
    if any(name not in files for name in (*REQUIRED_FILES, *NOTICES)):
        raise RuntimeError('Missing required curated resource or notice')
    before = {name: digest(path) for name, path in files.items()}
    archive_digest = digest(engine_source)
    manifest_digest = digest(manifest)
    source_files = {}
    prefix = 'new-horizons-' + source_commit + '/'
    with tarfile.open(engine_source, 'r:gz') as archive:
        for member in archive.getmembers():
            name = member.name
            if not name.startswith(prefix) or '..' in PurePosixPath(name).parts or '\\' in name:
                raise RuntimeError('Unsafe or wrong source archive root')
            relative = name[len(prefix):]
            if relative in source_files:
                raise RuntimeError('Duplicate source archive member')
            source_files[relative] = member

        def source_bytes(name):
            member = source_files.get(name)
            if member is None or not member.isfile():
                raise RuntimeError('Missing regular source archive member: ' + name)
            with archive.extractfile(member) as stream:
                return stream.read()

        if source_bytes('assets/new-horizons/runtime-art-manifest.json') != manifest.read_bytes():
            raise RuntimeError('Runtime art manifest differs from engine source')
        expected = {name for name, member in source_files.items()
                    if member.isfile() and any(name.startswith(tree + '/') for tree in TREES)
                    and name != 'config/newHorizonsMagicAssets.json'
                    and not re.match(r'Mods/new-horizons/(Images|Content/(sprites|data))(/|$)', name)}
        if set(files) - {'new-horizons', 'libvcmi.so'} != expected | NOTICES.keys():
            raise RuntimeError('Installed curated inventory differs from source/CMake notice mapping')
        for name, path in files.items():
            if name in ('new-horizons', 'libvcmi.so'):
                continue
            origin = NOTICES.get(name, name)
            # Current source exporters include NHART_* (not excluded NH_*).
            # Only historical archives lacking those two members need Git.
            data = (source_notice(source_repository, source_commit, origin)
                    if origin in ('docs/NHART_FORMAT.md', 'docs/NHART_DELIVERY.md')
                    and origin not in source_files else source_bytes(origin))
            if path.read_bytes() != data:
                raise RuntimeError('Installed bytes differ from source: ' + name)
        additions = {name: source_bytes(origin) for name, origin in {
            'new-horizons-launch.sh': 'tools/new-horizons-launch.sh',
            'license.txt': 'license.txt', 'AUTHORS.h': 'AUTHORS.h'}.items()}
        # All admission checks precede output creation; no source archive extraction.
        require_clean(install / 'bin')
        require_clean(install / 'lib')
        output.mkdir(parents=True, exist_ok=False)
        for name, path in files.items():
            target = output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        for name, data in additions.items():
            (output / name).write_bytes(data)
    (output / 'new-horizons').chmod(0o755)
    (output / 'new-horizons-launch.sh').chmod(0o755)
    if any(digest(output / name) != value or digest(files[name]) != value for name, value in before.items()):
        raise RuntimeError('Installed input changed during assembly')
    if digest(engine_source) != archive_digest or digest(manifest) != manifest_digest:
        raise RuntimeError('Source archive or art manifest changed during assembly')
    verify_install(output, manifest)
    require_clean(output)
    identity = {'source_commit': source_commit, 'client_executable': 'new-horizons',
                'source_archive_sha256': archive_digest,
                'binaries': {name: before[name] for name in ('new-horizons', 'libvcmi.so')},
                'runtime_art_manifest_sha256': manifest_digest, 'art_verification': art,
                'scope': 'Raw public Linux candidate; resource correspondence checked; separate BUILD-PROVENANCE, dependency/license and runtime acceptance required'}
    (output / 'BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2, sort_keys=True) + '\n')
    paths = sorted(p for p in output.rglob('*') if p.is_file())
    (output / 'SHA256SUMS').write_text(''.join(digest(p) + '  ' + p.relative_to(output).as_posix() + '\n' for p in paths))
    verify_candidate(output)
    for path in output.rglob('*'):
        path.chmod(0o555 if path.is_dir() or path.name in ('new-horizons', 'new-horizons-launch.sh') else 0o444)
    output.chmod(0o555)
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('install', 'engine-source', 'source-repository', 'manifest', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    print(json.dumps(assemble(**vars(parser.parse_args())), indent=2))


if __name__ == '__main__':
    main()
