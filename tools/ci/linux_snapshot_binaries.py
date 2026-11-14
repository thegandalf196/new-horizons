"""Bounded ELF admission for optional immutable Linux snapshot payloads."""
import os
import hashlib
from pathlib import Path
import re
import stat

RUNTIME_LIBRARY_RE = re.compile(r"lib[A-Za-z0-9_+-][A-Za-z0-9_.+-]*\.so(?:\.[0-9]+)*\Z")
SETUP_NAME = 'new-horizons-setup'
SUPPORT = {'Play-New-Horizons.sh': 'tools/Play-New-Horizons.sh',
           'new-horizons-launch.sh': 'tools/new-horizons-launch.sh',
           'linux_assets_setup.py': 'tools/linux_assets_setup.py'}


def runtime_library_payload(values, occupied):
    files = {}
    for value in values:
        extra = Path(value)
        if extra.is_symlink() or not extra.is_file():
            raise RuntimeError('Runtime library must be a regular non-symlink file')
        if not RUNTIME_LIBRARY_RE.fullmatch(extra.name):
            raise RuntimeError('Runtime library requires a safe lib*.so[.version] basename')
        if extra.name in occupied or extra.name in files:
            raise RuntimeError('Runtime library basename collision: ' + extra.name)
        with extra.open('rb') as stream:
            header = stream.read(64)
        if (len(header) < 52 or header[:4] != b'\x7fELF'
                or header[4] not in (1, 2) or header[5] not in (1, 2)
                or header[6] != 1 or (header[4] == 2 and len(header) < 64)
                or int.from_bytes(header[16:18], 'little' if header[5] == 1 else 'big') != 3
                or int.from_bytes(header[20:24], 'little' if header[5] == 1 else 'big') != 1):
            raise RuntimeError('Runtime library must have a valid ELF shared-object header')
        files[extra.name] = extra
    return files


def executable_abi(path):
    if path.is_symlink() or not path.is_file() or not os.access(path, os.X_OK):
        raise RuntimeError('Setup requires regular executable ELF files')
    if path.stat().st_mode & 0o7000:
        raise RuntimeError('Setup executable must not carry special permission bits')
    with path.open('rb') as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:7] not in (b'\x7fELF\x02\x01\x01', b'\x7fELF\x01\x01\x01'):
            raise RuntimeError('Setup requires a supported little-endian ELF executable')
        wide = header[4] == 2
        number = lambda offset, size: int.from_bytes(header[offset:offset + size], 'little')
        kind = number(16, 2)
        entry = number(24, 8 if wide else 4)
        offset = number(32 if wide else 28, 8 if wide else 4)
        stride = number(54 if wide else 42, 2)
        count = number(56 if wide else 44, 2)
        if kind not in (2, 3) or not entry or stride != (56 if wide else 32) or not 0 < count <= 1024:
            raise RuntimeError('Setup requires an ELF program, not a shared library')
        if offset < (64 if wide else 52) or offset + stride * count > path.stat().st_size:
            raise RuntimeError('Setup ELF program headers are invalid')
        stream.seek(offset)
        segments = stream.read(stride * count)
        if kind == 3 and not any(int.from_bytes(segments[i:i + 4], 'little') == 3
                                 for i in range(0, len(segments), stride)):
            raise RuntimeError('Setup PIE executable is missing its program interpreter')
        return header[4], header[5], number(18, 2)


def setup_payload(setup, client, source_root):
    if setup is None:
        return {}
    setup = Path(setup)
    if setup.name != SETUP_NAME or executable_abi(setup) != executable_abi(Path(client)):
        raise RuntimeError('Setup executable name or ELF architecture differs from the client')
    result = {SETUP_NAME: setup}
    for name, relative in SUPPORT.items():
        path = source_root / relative
        if path.is_symlink() or not path.is_file() or (name.endswith('.sh') and not os.access(path, os.X_OK)):
            raise RuntimeError('Missing regular setup support file: ' + name)
        result[name] = path
    return result


def setup_metadata(root, files, frozen=False):
    names = {SETUP_NAME, *SUPPORT}
    present = names.intersection(files)
    if not present:
        return None
    if present != names:
        raise RuntimeError('Snapshot setup payload is incomplete')
    if executable_abi(root / SETUP_NAME) != executable_abi(root / 'new-horizons'):
        raise RuntimeError('Snapshot setup ELF architecture differs from the client')
    result = {}
    for name in sorted(names):
        path = root / name
        mode = stat.S_IMODE(path.stat().st_mode)
        if mode & 0o7000 or (frozen and mode & 0o222):
            raise RuntimeError('Snapshot setup payload permissions are unsafe')
        if name.endswith('.sh') and not os.access(path, os.X_OK):
            raise RuntimeError('Snapshot setup launch script is not executable')
        result[name] = {'sha256': files[name], 'mode': mode & ~0o222}
    return result


def admit_setup_evidence(candidate, identity, reports):
    """Require the optional helper to be an independently checked ELF root."""
    if not (candidate / SETUP_NAME).exists():
        return
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
             for p in candidate.iterdir() if p.is_file()}
    setup_metadata(candidate, files)
    digest = files[SETUP_NAME]
    label = 'bundled/' + SETUP_NAME
    elf = reports['ELF-DEPENDENCIES.json']
    node = elf['graph'].get(label, {})
    if (identity.get('binaries', {}).get(SETUP_NAME) != digest
            or node.get('SHA256') != digest
            or elf.get('shipped_strong_dynamic_imports_resolved', {}).get(label, 0) <= 0
            or elf.get('unresolved_strong_imports', {}).get(label) != []):
        raise RuntimeError('Setup ELF requires matching source identity and independently resolved import evidence')
    for report in reports.values():
        if report.get('binaries', {}).get(SETUP_NAME) != digest:
            raise RuntimeError('Setup ELF differs from reviewed build/dependency evidence')
        report['packaged_elfs'][SETUP_NAME] = digest
