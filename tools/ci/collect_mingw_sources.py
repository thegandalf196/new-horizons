#!/usr/bin/env python3
"""Recover exact distro/upstream source archives for the audited local GNU DLLs.

No compiler is invoked. Ubuntu's original signed descriptors are preserved;
SHA256/size checks are enforced, not claimed as independent PGP verification.
"""
import argparse
from email.parser import Parser
import hashlib
import json
from pathlib import Path
import re
import urllib.parse
import urllib.request

from package_new_horizons_windows import sha256, write_json

GCC_PACKAGE = 'gcc-mingw-w64-x86-64-posix-runtime 13.2.0-6ubuntu1+26.1 gcc-mingw-w64 26.1'
PTHREAD_PACKAGE = 'mingw-w64-x86-64-dev 13.0.0-2ubuntu1 mingw-w64 13.0.0-2ubuntu1'
CATALOG = (
    ('gcc-mingw-w64', '26.1',
     'https://archive.ubuntu.com/ubuntu/pool/universe/g/gcc-mingw-w64/',
     '9cc5a84101b1c1641c001b43e457ea710619c5c8301151f0080a917e9fe64272'),
    ('mingw-w64', '13.0.0-2ubuntu1',
     'https://archive.ubuntu.com/ubuntu/pool/universe/m/mingw-w64/',
     '88e019a0cc74f8433c0b2bdaed7f0930a2fdc5d8295836b601d57dd806d0b49d'),
    ('gcc-13', '13.2.0-6ubuntu1',
     'https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/gcc-13/13.2.0-6ubuntu1/',
     '49a7446a379bfa3efeafcbf7fd6fd812d7319f516aceba1806777d9c53bf4472'),
)


def source_entries(data, source, version):
    text = data.decode('utf-8')
    if not text.startswith('-----BEGIN PGP SIGNED MESSAGE-----\n'):
        raise RuntimeError('Expected original signed source descriptor')
    body = text.split('\n\n', 1)[1].split('\n-----BEGIN PGP SIGNATURE-----', 1)[0]
    message = Parser().parsestr(body)
    if message['Source'] != source or message['Version'] != version:
        raise RuntimeError('Source descriptor identity mismatch')
    entries = []
    for line in (message['Checksums-Sha256'] or '').splitlines():
        if not line.strip():
            continue
        digest, size, name = line.split()
        if not re.fullmatch(r'[0-9a-f]{64}', digest) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9+_.~-]*', name):
            raise RuntimeError('Unsafe source archive descriptor entry')
        size = int(size)
        if not 0 < size <= 512 * 1024 * 1024:
            raise RuntimeError('Unexpected source archive size')
        entries.append({'file': name, 'size': size, 'sha256': digest})
    if not entries or len({e['file'] for e in entries}) != len(entries):
        raise RuntimeError('Missing or duplicate source archive entries')
    return entries


def validate_runtime_provenance(records):
    expected = {name: GCC_PACKAGE for name in ('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libssp-0.dll')}
    expected['libwinpthread-1.dll'] = PTHREAD_PACKAGE
    if len(records) != len(expected) or {r['file'] for r in records} != expected.keys():
        raise RuntimeError('Unexpected GNU runtime inventory')
    for record in records:
        if record['distribution_package'] != expected[record['file']] or not re.fullmatch(r'[0-9a-f]{64}', record['sha256']):
            raise RuntimeError('GNU source catalog does not match runtime provenance')


def fetch_verified(url, output, digest, size=None):
    if output.is_symlink():
        raise RuntimeError('Linked source evidence is not accepted: ' + output.name)
    if output.exists():
        if sha256(output) != digest or (size is not None and output.stat().st_size != size):
            raise RuntimeError('Refusing to replace mismatched source evidence: ' + output.name)
        return
    limit = size if size is not None else 512 * 1024
    temporary = output.with_name(output.name + '.part')
    checksum = hashlib.sha256()
    received = 0
    created = False
    try:
        with temporary.open('xb') as stream:
            created = True
            with urllib.request.urlopen(url, timeout=90) as response:
                while chunk := response.read(1024 * 1024):
                    received += len(chunk)
                    if received > limit:
                        raise RuntimeError('Source download exceeds declared size')
                    checksum.update(chunk)
                    stream.write(chunk)
        if checksum.hexdigest() != digest or (size is not None and received != size):
            raise RuntimeError('Source download hash/size mismatch: ' + output.name)
        temporary.replace(output)
    finally:
        if created and temporary.is_file():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-audit', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    runtimes = json.loads(args.runtime_audit.read_text())['gnu_runtime']
    validate_runtime_provenance(runtimes)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    sources = []
    for source, version, base, descriptor_hash in CATALOG:
        name = f'{source}_{version}.dsc'
        descriptor = args.output_dir / name
        fetch_verified(base + urllib.parse.quote(name), descriptor, descriptor_hash)
        entries = source_entries(descriptor.read_bytes(), source, version)
        for entry in entries:
            url = base + urllib.parse.quote(entry['file'])
            fetch_verified(url, args.output_dir / entry['file'], entry['sha256'], entry['size'])
            entry['url'] = url
        sources.append({'source': source, 'version': version, 'descriptor': name,
                        'descriptor_sha256': descriptor_hash, 'files': entries})
    write_json(args.output_dir / 'GNU-RUNTIME-SOURCES.json', {
        'runtimes': runtimes, 'sources': sources,
        'scope': 'Exact installed distro source identities and descriptor-pinned complete archives; no rebuild or Windows execution claim',
        'pgp_signatures': 'Original signatures preserved; independent signature verification not performed',
    })
    print('PASS: three exact source packages recovered and all declared file hashes/sizes verified')


if __name__ == '__main__':
    main()
