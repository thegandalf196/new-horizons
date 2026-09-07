"""Bind shipped CRT bytes to CMake's selected installed redistributables."""
import hashlib
import json
from pathlib import Path
import re


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_version(path):
    import pefile
    with pefile.PE(str(path)) as image:
        info = getattr(image, 'VS_FIXEDFILEINFO', [])
        if len(info) != 1:
            raise RuntimeError('CRT file version is missing or ambiguous')
        if image.FILE_HEADER.Machine != 0x8664:
            raise RuntimeError('CRT must be AMD64')
        item = info[0]
        return '.'.join(str(x) for x in (item.FileVersionMS >> 16,
                       item.FileVersionMS & 65535, item.FileVersionLS >> 16,
                       item.FileVersionLS & 65535))


def selected_redist(build_directory, visual_studio):
    cache = (build_directory / 'CMakeCache.txt').read_text(encoding='utf-8')
    roots = re.findall(r'^MSVC_REDIST_DIR:[^=]+=(.+)$', cache, re.MULTILINE)
    if len(roots) != 1:
        raise RuntimeError('CMake selected CRT directory is missing or ambiguous')
    vs = visual_studio.resolve(strict=True)
    selected = Path(roots[0].strip()).resolve(strict=True)
    allowed = (vs / 'VC/Redist/MSVC').resolve(strict=True)
    if selected == allowed or not selected.is_relative_to(allowed):
        raise RuntimeError('CMake selected CRT directory is outside the VS redist tree')
    return vs, selected


def preflight_runtime(build_directory, visual_studio):
    """Inspect selected redistributables before compilation; return metadata only."""
    vs, selected = selected_redist(build_directory, visual_studio)
    records = []
    for source in sorted((selected / 'x64').glob('Microsoft.VC*.CRT/*.dll')):
        if not source.name.lower().startswith(('msvcp', 'vcruntime', 'concrt')):
            continue
        resolved = source.resolve(strict=True)
        if not resolved.is_relative_to(selected):
            raise RuntimeError('CRT source escapes selected redistributable directory')
        version = file_version(resolved)
        if not version.startswith('14.'):
            raise RuntimeError('Unreviewed CRT family requires notice review')
        records.append({'file': source.name, 'sha256': sha256(resolved),
                        'file_version': version, 'vs_relative_source': resolved.relative_to(vs).as_posix()})
    if not records:
        raise RuntimeError('No selected CRT sources found before compilation')
    return {'scope': 'Selected-source preflight, not final-package binding or historical origin proof',
            'cmake_selected_redist_directory': selected.relative_to(vs).as_posix(), 'runtimes': records}


def bind_runtime(package, build_directory, visual_studio):
    """Fail closed unless shipped CRT bytes match CMake-selected sources and terms.

    This is a final-package gate, not a notice-only preflight. No absolute source
    paths enter the distributed provenance; origins are relative to VSINSTALLDIR.
    """
    vs, selected = selected_redist(build_directory, visual_studio)
    binaries = sorted(p for p in package.glob('*.dll')
                      if p.name.lower().startswith(('msvcp', 'vcruntime', 'concrt')))
    if not binaries:
        raise RuntimeError('No shipped MSVC runtime found for provenance binding')
    records = []
    for binary in binaries:
        digest = sha256(binary)
        sources = []
        for folder in sorted((selected / 'x64').glob('Microsoft.VC*.CRT')):
            source = folder / binary.name
            if source.is_file():
                resolved = source.resolve(strict=True)
                if not resolved.is_relative_to(selected):
                    raise RuntimeError('CRT source escapes selected redistributable directory')
                if sha256(resolved) == digest:
                    sources.append(resolved.relative_to(vs).as_posix())
        if not sources:
            raise RuntimeError('Shipped CRT does not match selected redistributable bytes')
        version = file_version(binary)
        if not version.startswith('14.'):
            raise RuntimeError('Unreviewed CRT family requires notice review')
        records.append({'file': binary.name, 'sha256': digest,
                        'file_version': version, 'matching_vs_relative_sources': sources})
    path = package / 'licenses/Microsoft/PROVENANCE.json'
    provenance = json.loads(path.read_text(encoding='utf-8'))
    if not provenance.get('terms'):
        raise RuntimeError('CRT binding requires retained Microsoft terms')
    for term in provenance['terms']:
        name = term.get('file', '')
        if not name or Path(name).name != name:
            raise RuntimeError('Invalid retained Microsoft terms filename')
        retained = path.parent / name
        if not retained.is_file() or retained.is_symlink() or sha256(retained) != term.get('sha256'):
            raise RuntimeError('Retained Microsoft terms do not match provenance')
    provenance['cmake_selected_redist_directory'] = selected.relative_to(vs).as_posix()
    provenance['runtime_bindings'] = records
    provenance['runtime_binding_status'] = 'verified-byte-identical-selected-redist'
    path.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return records
