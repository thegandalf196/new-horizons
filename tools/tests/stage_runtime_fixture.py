"""Tiny synthetic staging evidence; no game, artwork, SDK or executable runs."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from unittest.mock import patch


QUARTET = ('libSDL3.so.0', 'libSDL3_image.so.0', 'libSDL3_mixer.so.0', 'libSDL3_ttf.so.0')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def elf_bytes(tag=0):
    data = bytearray(64)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHI', data, 16, 3, 62, 1)
    return bytes(data) + bytes([tag])


class StageFixture:
    def __init__(self, root, candidate=None, providers=QUARTET):
        self.root = Path(root)
        self.candidate = Path(candidate) if candidate else self.root / 'candidate'
        self.candidate.mkdir(exist_ok=True)
        self.arguments = {'candidate': self.candidate, 'output': self.root / 'stage'}
        for key in ('engine-source', 'dependency-source', 'packaging-source'):
            path = self.root / (key + '.tar.gz')
            path.write_bytes(key.encode())  # Opaque fixture inputs, never an audit claim.
            self.arguments[key] = path
        for key in ('notices', 'header-supplement'):
            self.arguments[key] = self.root / key
            self.arguments[key].mkdir()
        engines = {'new-horizons': b'fixture-client', 'libvcmi.so': b'fixture-facade'}
        for name, data in engines.items():
            (self.candidate / name).write_bytes(data)
        self.identity = {'source_commit': 'engine-revision',
                         'source_archive_sha256': digest(self.arguments['engine-source'].read_bytes()),
                         'binaries': {name: digest(data) for name, data in engines.items()}}
        common = {'source_commit': self.identity['source_commit'], 'binaries': self.identity['binaries']}
        self.elf = {**common, 'scope': 'Synthetic original engine-root evidence scope.',
                    'graph': {}, 'shipped_strong_dynamic_imports_resolved': {},
                    'unresolved_strong_imports': {}, 'external_ELF_nodes': len(providers)}
        for name, value in self.identity['binaries'].items():
            label = 'bundled/' + name
            self.elf['graph'][label] = {'SHA256': value, 'NEEDED': {}, 'RUNPATH_or_RPATH': ['$ORIGIN']}
            self.elf['shipped_strong_dynamic_imports_resolved'][label] = 1
            self.elf['unresolved_strong_imports'][label] = []
        self.inventory = {**common, 'declared_public_provider_ELFs': len(providers),
                          'declared_public_provider_files': {}, 'headers': {'public-provider/include/SDL.h': 'origin'},
                          'dependency_source_archive_sha256': digest(self.arguments['dependency-source'].read_bytes())}
        self.provenance = {**common, 'source_archive_sha256': self.identity['source_archive_sha256'],
                           'source_inputs': {'original.cpp': 'unchanged'}}
        self.libraries = []
        self.origins = {}
        for index, name in enumerate(providers):
            path = self.root / name
            self.libraries.append(path)
            label = 'public-provider/lib/' + name + '.1'
            self.origins[name] = label
            self.elf['graph']['bundled/new-horizons']['NEEDED'][name] = label
            self.elf['graph'][label] = {'SHA256': '', 'NEEDED': {}, 'RUNPATH_or_RPATH': []}
            self.set_payload(path, elf_bytes(index))
        if providers:
            first = providers[0]
            for name in providers[1:]:
                self.elf['graph'][self.origins[name]]['NEEDED'][first] = self.origins[first]
        self.write_candidate()

    def set_payload(self, path, data):
        path.write_bytes(data)
        label = self.origins[path.name]
        self.inventory['declared_public_provider_files'][label] = {'sha256': digest(data), 'bytes': len(data)}
        self.elf['graph'][label]['SHA256'] = digest(data)

    def write_candidate(self):
        (self.candidate / 'BUILD-IDENTITY.json').write_text(json.dumps(self.identity))
        paths = sorted(p for p in self.candidate.iterdir() if p.is_file() and p.name != 'SHA256SUMS')
        (self.candidate / 'SHA256SUMS').write_text(''.join(digest(p.read_bytes()) + '  ' + p.name + '\n' for p in paths))

    def run(self, main, libraries=None):
        for key, value in (('elf-audit', self.elf), ('inventory', self.inventory), ('build-provenance', self.provenance)):
            path = self.root / (key + '.json')
            path.write_text(json.dumps(value))
            self.arguments[key] = path
        argv = ['stage_linux_client.py', '--packaging-revision', 'packaging-revision']
        for key, value in self.arguments.items():
            argv.extend(['--' + key, str(value)])
        for path in self.libraries if libraries is None else libraries:
            argv.extend(['--runtime-library', str(path)])
        with patch.object(sys, 'argv', argv), patch('stage_linux_client.subprocess.check_output', return_value=b'Fixture package text\n'):
            main()


def tree_bytes(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}
