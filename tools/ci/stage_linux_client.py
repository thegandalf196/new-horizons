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
    require_clean(root)
    return json.loads((root / 'BUILD-IDENTITY.json').read_text())


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'notices', 'header-supplement', 'elf-audit', 'inventory',
                 'build-provenance', 'engine-source', 'dependency-source', 'packaging-source', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--packaging-revision', required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError('Refusing to replace an existing stage')
    identity = verify_candidate(args.candidate)
    if sha256(args.engine_source) != identity['source_archive_sha256']:
        raise RuntimeError('Engine source archive differs from candidate identity')
    root = Path(__file__).resolve().parents[2]
    package = args.output / 'New-Horizons-Linux-x64'
    shutil.copytree(args.candidate, package)
    package.chmod(0o755)
    # Metadata is regenerated in the new owned stage only; frozen input is intact.
    for name in ('SHA256SUMS', 'BUILD-IDENTITY.json'):
        (package / name).chmod(0o644)
    for source, name in (('tools/Play-New-Horizons.sh', 'Play-New-Horizons.sh'),
                         ('tools/linux/README-New-Horizons.txt', 'README-New-Horizons.txt'),
                         ('tools/linux/LEGAL-New-Horizons.txt', 'LEGAL-New-Horizons.txt'),
                         ('tools/linux/BUILDING-New-Horizons.txt', 'BUILDING-New-Horizons.txt')):
        data = subprocess.check_output(['git', '-C', str(root), 'show', args.packaging_revision + ':' + source])
        (package / name).write_bytes(data)
    (package / 'Play-New-Horizons.sh').chmod(0o755)
    copy_plain_tree(args.notices, package / 'licenses/system')
    terms = package / 'licenses/header-boundaries'
    terms.mkdir(parents=True)
    for source in args.header_supplement.iterdir():
        if source.is_file() and source.name != 'PROVENANCE.json':
            if source.is_symlink():
                raise RuntimeError('Header term symlink rejected')
            shutil.copy2(source, terms / source.name)
    for source, name in ((args.elf_audit, 'ELF-DEPENDENCIES.json'),
                         (args.inventory, 'DEPENDENCY-INVENTORY.json'),
                         (args.build_provenance, 'BUILD-PROVENANCE.json')):
        data = public_evidence(json.loads(source.read_text()), args.candidate)
        text = json.dumps(data, indent=2) + '\n'
        if inspect_bytes(text.encode()):
            raise RuntimeError('Personal profile path in public evidence')
        (package / name).write_text(text)
    sources = {}
    for source in (args.engine_source, args.dependency_source, args.packaging_source):
        if source.name in sources:
            raise RuntimeError('Duplicate source archive name')
        sources[source.name] = {'bytes': source.stat().st_size, 'sha256': sha256(source)}
    identity.update({'scope': 'Ubuntu26.04 system-dependent Linux preview; acceptance evidence is separate',
                     'packaging_commit': args.packaging_revision, 'source_archives': sources})
    (package / 'BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2) + '\n')
    sums = package / 'SHA256SUMS'
    sums.write_text(''.join(f'{sha256(p)}  {p.relative_to(package).as_posix()}\n'
                           for p in sorted(package.rglob('*')) if p.is_file() and p != sums))
    verify_candidate(package)
    for p in package.rglob('*'):
        p.chmod(p.stat().st_mode & ~0o222)
    package.chmod(0o555)
    print('Frozen staged payload:', package)


if __name__ == '__main__':
    main()
