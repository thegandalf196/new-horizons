#!/usr/bin/env python3
"""Stage an audited Linux candidate without changing its ELF or resource bytes.

This consumes separately reviewed source/dependency evidence. It does not grant
GUI, licensing, source-correspondence or cross-distribution acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from binary_privacy import inspect_bytes, require_clean
from client_features import client_feature_files
from linux_snapshot_binaries import SETUP_NAME, setup_metadata, admit_setup_evidence
from stage_runtime_libraries import admit_runtime_libraries, relocate_runtime_evidence, runtime_records


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_candidate(root):
    if not root.is_dir():
        raise RuntimeError('Candidate directory missing')
    entries = list(root.rglob('*'))
    if any(p.is_symlink() for p in entries):
        raise RuntimeError('Candidate symlinks are not permitted')
    expected = {}
    for line in (root / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        p = Path(name)
        if p.is_absolute() or '..' in p.parts or name in expected:
            raise RuntimeError('Unsafe or duplicate candidate manifest entry')
        expected[name] = digest
    actual = {p.relative_to(root).as_posix() for p in entries if p.is_file()}
    if actual != set(expected) | {'SHA256SUMS'}:
        raise RuntimeError('Candidate inventory differs from manifest')
    for name, digest in expected.items():
        if sha256(root / name) != digest:
            raise RuntimeError('Candidate checksum mismatch')
    if (root / 'new-horizons').is_file():
        client_feature_files(root / 'new-horizons')
    require_clean(root)
    identity = json.loads((root / 'BUILD-IDENTITY.json').read_text())
    if (root / SETUP_NAME).exists():
        setup = setup_metadata(root, expected)
        if 'setup' in identity and identity['setup'] != setup:
            raise RuntimeError('Setup payload differs from its staged identity')
    return identity


def public_evidence(value, candidate):
    if isinstance(value, dict):
        return {public_evidence(k, candidate): public_evidence(v, candidate) for k, v in value.items()}
    if isinstance(value, list):
        return [public_evidence(v, candidate) for v in value]
    if isinstance(value, str):
        prefix = candidate.resolve().as_posix() + '/'
        if value.startswith(prefix):
            return 'bundled/' + value[len(prefix):]
    return value


def copy_plain_tree(source, destination):
    if not source.is_dir() or any(p.is_symlink() for p in source.rglob('*')):
        raise RuntimeError('Evidence tree must exist without symlinks')
    shutil.copytree(source, destination)


def source_archives(paths):
    sources = {}
    for source in paths:
        record = {'bytes': source.stat().st_size, 'sha256': sha256(source)}
        if source.name in sources and sources[source.name] != record:
            raise RuntimeError('Conflicting source archive name')
        sources[source.name] = record
    return sources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'notices', 'header-supplement', 'elf-audit', 'inventory',
                 'build-provenance', 'engine-source', 'dependency-source', 'packaging-source', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--packaging-revision', required=True)
    parser.add_argument('--portable-updates', action='store_true', default=True,
                        help='Include the desktop launcher and updater (the player-package default)')
    parser.add_argument('--legacy-core-only', action='store_false', dest='portable_updates',
                        help='Explicitly stage a legacy diagnostic package without the desktop launcher')
    parser.add_argument('--release-archive-dir', type=Path,
                        help='Write the updater-compatible official Linux archive after staging verification')
    parser.add_argument('--runtime-library', type=Path, action='append', default=[],
                        help='regular ELF provider named by its incoming SONAME; repeat for the complete declared set')
    args = parser.parse_args()
    if args.release_archive_dir is not None and not args.portable_updates:
        raise RuntimeError('Official release archives require the desktop launcher')
    if args.output.exists() or args.output.is_symlink():
        raise RuntimeError('Refusing to replace an existing stage')
    sources = source_archives((args.engine_source, args.dependency_source, args.packaging_source))
    identity = verify_candidate(args.candidate)
    if sources[args.engine_source.name]['sha256'] != identity['source_archive_sha256']:
        raise RuntimeError('Engine source archive differs from candidate identity')
    reports = {name: public_evidence(json.loads(source.read_text()), args.candidate)
               for source, name in ((args.elf_audit, 'ELF-DEPENDENCIES.json'),
                                    (args.inventory, 'DEPENDENCY-INVENTORY.json'),
                                    (args.build_provenance, 'BUILD-PROVENANCE.json'))}
    libraries = admit_runtime_libraries(args.runtime_library, args.candidate, identity,
                                       reports, sources[args.dependency_source.name]['sha256'])
    reports = relocate_runtime_evidence(reports, libraries)
    admit_setup_evidence(args.candidate, identity, reports)
    report_text = {name: json.dumps(data, indent=2) + '\n' for name, data in reports.items()}
    if any(inspect_bytes(text.encode()) for text in report_text.values()):
        raise RuntimeError('Personal profile path in public evidence')
    root = Path(__file__).resolve().parents[2]
    package = args.output / 'New-Horizons-Linux-x64'
    shutil.copytree(args.candidate, package)
    package.chmod(0o755)
    # Metadata is regenerated in the new owned stage only; frozen input is intact.
    for name in ('SHA256SUMS', 'BUILD-IDENTITY.json'):
        (package / name).chmod(0o644)
    for name, record in libraries.items():
        (package / name).write_bytes(record['payload'])  # Owned verified bytes; never modify the input provider.
    player_files = [('tools/Play-New-Horizons.sh', 'Play-New-Horizons.sh'),
                         ('tools/new-horizons-launch.sh', 'new-horizons-launch.sh'),
                         ('tools/linux_assets_setup.py', 'linux_assets_setup.py'),
                         ('tools/linux/README-New-Horizons.txt', 'README-New-Horizons.txt'),
                         ('tools/linux/LEGAL-New-Horizons.txt', 'LEGAL-New-Horizons.txt'),
                         ('tools/linux/BUILDING-New-Horizons.txt', 'BUILDING-New-Horizons.txt')]
    if args.portable_updates:
        player_files.extend((f'tools/linux/{name}', name) for name in
                            ('PortableReleaseLauncher.py', 'PortableReleaseUpdate.py',
                             'PortableLauncherWindow.py', 'PortableLauncherProcess.py'))
    for source, name in player_files:
        data = subprocess.check_output(['git', '-C', str(root), 'show', args.packaging_revision + ':' + source])
        if (package / name).exists():
            (package / name).chmod(0o644)
        (package / name).write_bytes(data)
    (package / 'Play-New-Horizons.sh').chmod(0o755)
    (package / 'new-horizons-launch.sh').chmod(0o755)
    if (package / SETUP_NAME).exists():
        identity['setup'] = setup_metadata(package, {p.name: sha256(p) for p in package.iterdir() if p.is_file()})
    copy_plain_tree(args.notices, package / 'licenses/system')
    terms = package / 'licenses/header-boundaries'
    terms.mkdir(parents=True)
    for source in args.header_supplement.iterdir():
        if source.is_file() and source.name != 'PROVENANCE.json':
            if source.is_symlink():
                raise RuntimeError('Header term symlink rejected')
            shutil.copy2(source, terms / source.name)
    for name, text in report_text.items():
        (package / name).write_text(text)
    identity.update({'scope': 'Ubuntu26.04 system-dependent Linux preview; acceptance evidence is separate',
                     'packaging_commit': args.packaging_revision, 'source_archives': sources,
                     'bundled_runtime_libraries': runtime_records(libraries),
                     'packaged_elfs': reports['ELF-DEPENDENCIES.json']['packaged_elfs']})
    (package / 'BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2) + '\n')
    sums = package / 'SHA256SUMS'
    sums.write_text(''.join(f'{sha256(p)}  {p.relative_to(package).as_posix()}\n'
                           for p in sorted(package.rglob('*')) if p.is_file() and p != sums))
    verify_candidate(package)
    for p in package.rglob('*'):
        p.chmod(p.stat().st_mode & ~0o222)
    package.chmod(0o555)
    if args.release_archive_dir is not None:
        from archive_linux_player_release import archive_stage
        archive, checksum = archive_stage(package, args.release_archive_dir)
        print(checksum + '  ' + archive.name)
    print('Frozen staged payload:', package)


if __name__ == '__main__':
    main()
