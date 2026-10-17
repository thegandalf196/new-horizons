"""Admit declared runtime providers and relocate their reviewed stage evidence."""
import copy
import hashlib
from pathlib import Path, PurePosixPath
import re
import struct

from binary_privacy import inspect_bytes


SONAME = re.compile(r'lib[A-Za-z0-9_+-][A-Za-z0-9_.+-]*\.so(?:\.[0-9]+)*\Z')
ENGINE_ROOTS = {'new-horizons', 'libvcmi.so'}
PROVIDER_PREFIX = 'public-provider/'


def bind_reports(candidate, identity, reports, dependency_sha):
    engines = identity.get('binaries', {})
    if set(engines) != ENGINE_ROOTS:
        raise RuntimeError('Expected the two original engine identity roots')
    for report in reports.values():
        if (report.get('source_commit') != identity['source_commit']
                or report.get('binaries') != engines):
            raise RuntimeError('Dependency evidence differs from engine identity')
    for name, digest in engines.items():
        if hashlib.sha256((candidate / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Engine identity differs from candidate bytes')
    elf = reports['ELF-DEPENDENCIES.json']
    roots = {'bundled/' + name for name in engines}
    if {key for key in elf.get('graph', {}) if key.startswith('bundled/')} != roots:
        raise RuntimeError('Raw evidence must contain exactly the two bundled engine roots')
    checked = elf.get('shipped_strong_dynamic_imports_resolved', {})
    allowed = roots | {key for key in elf.get('graph', {}) if key.startswith(PROVIDER_PREFIX)}
    if (not roots <= set(checked) <= allowed
            or set(elf.get('unresolved_strong_imports', {})) != set(checked)
            or any(elf['unresolved_strong_imports'].values())
            or any(type(count) is not int or count < 0 for count in checked.values())):
        raise RuntimeError('Expected resolved strong-import evidence for identified graph roots')
    for name, digest in engines.items():
        if elf.get('graph', {}).get('bundled/' + name, {}).get('SHA256') != digest:
            raise RuntimeError('ELF graph differs from engine identity')
    if reports['BUILD-PROVENANCE.json'].get('source_archive_sha256') != identity['source_archive_sha256']:
        raise RuntimeError('Build provenance differs from engine source')
    if reports['DEPENDENCY-INVENTORY.json'].get('dependency_source_archive_sha256') != dependency_sha:
        raise RuntimeError('Runtime providers require their matching source companion')


def expected_providers(elf, inventory):
    graph = elf.get('graph', {})
    providers = {label for label in graph if label.startswith(PROVIDER_PREFIX)}
    if inventory.get('declared_public_provider_ELFs') != len(providers):
        raise RuntimeError('Declared provider graph and inventory count differ')
    incoming = {label: set() for label in providers}
    for node in graph.values():
        for name, target in node.get('NEEDED', {}).items():
            if target.startswith(PROVIDER_PREFIX):
                if target not in incoming:
                    raise RuntimeError('Needed declared provider is absent from graph')
                incoming[target].add(name)
    result = {}
    for label, names in incoming.items():
        if len(names) != 1 or '..' in PurePosixPath(label).parts or '\\' in label:
            raise RuntimeError('Provider needs one unambiguous incoming SONAME')
        name = next(iter(names))
        if not SONAME.fullmatch(name) or name in result:
            raise RuntimeError('Unsafe or duplicate provider SONAME')
        pin = inventory.get('declared_public_provider_files', {}).get(label, {})
        if (not re.fullmatch('[0-9a-f]{64}', pin.get('sha256', ''))
                or type(pin.get('bytes')) is not int or not 0 < pin['bytes'] <= 128 * 1024 * 1024
                or graph[label].get('SHA256') != pin['sha256']):
            raise RuntimeError('Provider graph lacks exact inventory hash and size')
        result[name] = {'origin_label': label, 'sha256': pin['sha256'], 'bytes': pin['bytes']}
    return result


def admit_runtime_libraries(paths, candidate, identity, reports, dependency_sha):
    bind_reports(candidate, identity, reports, dependency_sha)
    expected = expected_providers(reports['ELF-DEPENDENCIES.json'], reports['DEPENDENCY-INVENTORY.json'])
    if len(paths) != len(expected) or len(paths) > 128:
        raise RuntimeError('Supply every declared runtime provider exactly once')
    admitted = {}
    for value in paths:
        path = Path(value)
        name = path.name
        if (not SONAME.fullmatch(name) or name not in expected or name in admitted
                or '..' in path.parts or path.is_symlink() or not path.is_file()
                or any(parent.is_symlink() for parent in path.parents)
                or (candidate / name).exists() or (candidate / name).is_symlink()):
            raise RuntimeError('Unsafe, duplicate, undeclared or colliding runtime library')
        pin = expected[name]
        if path.stat().st_size != pin['bytes']:
            raise RuntimeError('Runtime provider size differs from inventory')
        payload = path.read_bytes()
        if hashlib.sha256(payload).hexdigest() != pin['sha256']:
            raise RuntimeError('Runtime provider hash differs from reviewed evidence')
        if (len(payload) < 64 or payload[:7] != b'\x7fELF\x02\x01\x01'
                or struct.unpack_from('<HHI', payload, 16) != (3, 62, 1)):
            raise RuntimeError('Runtime provider must be ELF64 little-endian AMD64 ET_DYN')
        if inspect_bytes(payload):
            raise RuntimeError('Personal profile path in runtime library')
        admitted[name] = {**pin, 'payload': payload,
                          'source_archive_sha256': dependency_sha}
    if set(admitted) != set(expected):
        raise RuntimeError('Missing declared runtime provider; no fallback permitted')
    return admitted


def runtime_records(libraries):
    return {name: {key: record[key] for key in
                   ('origin_label', 'sha256', 'bytes', 'source_archive_sha256')}
            for name, record in libraries.items()}


def relocate_runtime_evidence(reports, libraries):
    result = copy.deepcopy(reports)
    records = runtime_records(libraries)
    labels = {record['origin_label']: 'bundled/' + name for name, record in records.items()}
    elf = result['ELF-DEPENDENCIES.json']
    graph = {}
    for label, node in elf['graph'].items():
        relocated = labels.get(label, label)
        if relocated in graph:
            raise RuntimeError('Bundled provider label collision')
        node['NEEDED'] = {name: labels.get(target, target) for name, target in node['NEEDED'].items()}
        graph[relocated] = node
    elf['graph'] = graph
    elf['external_ELF_nodes'] = len(graph) - len(ENGINE_ROOTS) - len(libraries)
    elf['bundled_runtime_provider_ELFs'] = len(libraries)
    for key in ('shipped_strong_dynamic_imports_resolved', 'unresolved_strong_imports'):
        elf[key] = {labels.get(label, label): value for label, value in elf[key].items()}
    elf['strong_symbol_check_roots'] = sorted(elf['shipped_strong_dynamic_imports_resolved'])
    elf['staging_relocation_scope'] = 'Reviewed checked-root keys and dependency labels relocated; staging performs no new symbol checks. Original scope is unchanged.'
    inventory = result['DEPENDENCY-INVENTORY.json']
    inventory['declared_public_provider_origin_ELFs'] = inventory['declared_public_provider_ELFs']
    inventory['declared_public_provider_ELFs'] = 0
    inventory['bundled_runtime_provider_ELFs'] = len(libraries)
    inventory['runtime_provider_scope'] = 'Declared runtime providers are bundled; original API/header/source origin records are unchanged.'
    for report in result.values():
        report['bundled_runtime_libraries'] = records
        report['binary_evidence_scope'] = 'binaries identifies the original two engine roots; ELF strong_symbol_check_roots lists only actually reviewed checks, without new staging analysis.'
        report['packaged_elfs'] = {**report['binaries'], **{name: record['sha256'] for name, record in records.items()}}
    return result
