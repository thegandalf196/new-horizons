#!/usr/bin/env python3
"""Read-only PE inspection and explicit deployment repairs for the local MinGW lane.

Requires pefile (tested with 2024.8.26). Never renames or modifies cached packages.
This proves imports/exports and identity, not Windows execution or license coverage.
"""
import shutil
import subprocess
from pathlib import Path

from package_new_horizons_windows import SYSTEM_DLLS, sha256

GNU_RUNTIME_DLLS = ('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll', 'libssp-0.dll')


def inspect_pe(path):
    import pefile
    with pefile.PE(str(path), fast_load=True) as image:
        if image.FILE_HEADER.Machine != 0x8664 or image.OPTIONAL_HEADER.Magic != 0x20b:
            raise RuntimeError('Not an AMD64 PE32+ image: ' + path.name)
        image.parse_data_directories(directories=[0, 1, 13])
        imports = []
        for directory in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
            for module in getattr(image, directory, []):
                name = module.dll.decode('ascii').lower()
                if Path(name).name != name or '/' in name or '\\' in name:
                    raise RuntimeError('Path-bearing PE import: ' + name)
                for symbol in module.imports:
                    imports.append({'dll': name, 'name': symbol.name.decode('ascii') if symbol.name else None,
                                    'ordinal': symbol.ordinal, 'delay': directory.endswith('DELAY_IMPORT')})
        names, ordinals, forwarders = set(), set(), []
        export_name = None
        if hasattr(image, 'DIRECTORY_ENTRY_EXPORT'):
            export_name = image.DIRECTORY_ENTRY_EXPORT.name.decode('ascii').lower()
            for symbol in image.DIRECTORY_ENTRY_EXPORT.symbols:
                if symbol.name:
                    names.add(symbol.name.decode('ascii'))
                ordinals.add(symbol.ordinal)
                if symbol.forwarder:
                    target = symbol.forwarder.decode('ascii')
                    module, separator, entry = target.rpartition('.')
                    if not separator or not module or not entry or '/' in module or '\\' in module:
                        raise RuntimeError('Malformed PE forwarder: ' + target)
                    if not module.lower().endswith('.dll'):
                        module += '.dll'
                    forwarders.append({'dll': module.lower(), 'name': None if entry.startswith('#') else entry,
                                       'ordinal': int(entry[1:]) if entry.startswith('#') else None, 'delay': False})
        return {'machine': 'AMD64', 'subsystem': image.OPTIONAL_HEADER.Subsystem,
                'imports': imports, 'forwarders': forwarders, 'export_names': names,
                'export_ordinals': ordinals, 'export_dll_name': export_name}


def stage_gnu_runtime(destination, compiler='x86_64-w64-mingw32-g++-posix'):
    """Copy exact compiler-distributed DLL bytes, retaining package-owner provenance."""
    result = []
    for name in GNU_RUNTIME_DLLS:
        source = Path(subprocess.check_output([compiler, '-print-file-name=' + name], text=True).strip())
        if not source.is_absolute() or not source.is_file():
            raise RuntimeError('Compiler runtime not found: ' + name)
        source = source.resolve(strict=True)
        inspect_pe(source)
        target = destination / name
        if target.exists():
            raise RuntimeError('Runtime deployment collision: ' + name)
        owner = subprocess.check_output(['dpkg-query', '-S', str(source)], text=True).strip().rsplit(': ', 1)[0]
        package = subprocess.check_output(['dpkg-query', '-W', '-f=${Package} ${Version} ${source:Package} ${source:Version}', owner], text=True).strip()
        shutil.copyfile(source, target)
        if sha256(source) != sha256(target):
            raise RuntimeError('Compiler runtime copy changed bytes: ' + name)
        result.append({'file': name, 'sha256': sha256(target), 'distribution_package': package,
                       'scope': 'Exact installed compiler runtime; corresponding source/notices still required'})
    return result


def stage_ogg_loader_alias(destination):
    """The exact Ogg DLL and import archive name ogg.dll despite the libogg.dll file.

    Keep the original and add a byte-identical loader alias in our new stage only.
    No arbitrary rename, ABI substitution, cached-file edit or source claim.
    """
    source, target = destination / 'libogg.dll', destination / 'ogg.dll'
    if not source.is_file() or target.exists():
        raise RuntimeError('Unexpected Ogg deployment layout')
    if inspect_pe(source)['export_dll_name'] != 'ogg.dll':
        raise RuntimeError('Ogg export identity does not justify the loader alias')
    shutil.copyfile(source, target)
    if sha256(source) != sha256(target):
        raise RuntimeError('Ogg loader alias changed bytes')
    return {'source_file': source.name, 'loader_alias': target.name, 'sha256': sha256(target),
            'reason': 'Original export directory and import archive explicitly name ogg.dll'}


def audit_directory(directory):
    """Validate all shipped images, including delay imports and forwarded symbols."""
    files = [p for p in directory.iterdir() if p.is_file() and p.suffix.lower() in {'.exe', '.dll'}]
    by_name = {p.name.lower(): p for p in files}
    if len(by_name) != len(files):
        raise RuntimeError('Case-insensitive PE filename collision')
    if not {'vcmi_client.exe', 'vcmi_lib.dll'} <= by_name.keys():
        raise RuntimeError('Missing client/facade image')
    parsed = {name: inspect_pe(path) for name, path in by_name.items()}
    report = {}
    for name, image in parsed.items():
        resolved = {}
        for symbol in image['imports'] + image['forwarders']:
            dependency = symbol['dll']
            if dependency in parsed:
                target = parsed[dependency]
                available = (symbol['name'] in target['export_names'] if symbol['name'] is not None
                             else symbol['ordinal'] in target['export_ordinals'])
                if not available:
                    raise RuntimeError(f"Missing PE export: {name} -> {dependency}: {symbol['name'] or symbol['ordinal']}")
                resolved[dependency] = 'bundled; requested exports present'
            elif dependency in SYSTEM_DLLS or dependency.startswith(('api-ms-win-', 'ext-ms-win-')):
                resolved[dependency] = 'Windows system/API contract; runtime OS availability unverified'
            else:
                raise RuntimeError(f'Unresolved non-system PE import: {name} -> {dependency}')
        report[name] = {'sha256': sha256(by_name[name]), 'machine': image['machine'],
                        'subsystem': image['subsystem'], 'dependencies': resolved,
                        'checked_imports_and_forwarders': len(image['imports']) + len(image['forwarders'])}
    return report
