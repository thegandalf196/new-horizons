#!/usr/bin/env python3
"""Collect independent, safe notice/source failures across the resolved host graph.

Partial output is never publishable. Unsafe extraction or an unclassified source
command failure aborts immediately, with a structured failure report retained.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil

from package_new_horizons_windows import collect_notices, sha256, write_json
from binary_privacy import inspect_bytes, verified_github_ci_provenance

SAFE_FAILURES = {
    'Missing dependency license texts': 'missing-notice',
    'Missing verified SQLite upstream public-domain notice': 'missing-sqlite-notice',
    'Missing FFmpeg source license text': 'missing-ffmpeg-terms',
    'Missing exact cached recipe for dependency': 'missing-recipe',
    'Missing or mismatched exact exported sources': 'missing-verified-exports',
}

# Conan Center's pinned dav1d recipe currently has one canonical source URL.
# Keep the fallback bytes tied to the exact recipe source checksum.  These are
# public VideoLAN mirrors of the same release archive; the checksum comes from
# the upstream .sha256 sidecar and is independently pinned here so a mirror
# cannot choose the bytes accepted by CI.
DAV1D_SOURCE_REFERENCE = 'dav1d/1.5.4'
DAV1D_SOURCE_SHA256 = '686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5'
DAV1D_SOURCE_MIRROR_URLS = (
    'https://videolan.c3sl.ufpr.br/dav1d/1.5.4/dav1d-1.5.4.tar.xz',
    'https://videolan.mirror.garr.it/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz',
)


def _verified_cache_directory(path, label):
    path = Path(path)
    if path.is_symlink() or (path.exists() and not path.is_dir()):
        raise RuntimeError(f'{label} must be a real directory')
    path.mkdir(parents=True, exist_ok=True)
    return path


def seed_source_cache(cache_root, archive, expected_sha256):
    """Install one already-downloaded source archive into Conan's source cache.

    Conan's ``core.sources:download_cache`` stores a source archive as
    ``<cache>/s/<sha256>``.  This helper deliberately accepts only an existing
    local file: downloading remains a workflow concern, while this function
    gives the preflight a single, tested integrity gate before Conan can use
    the bytes.  Destination creation is exclusive and an existing entry must
    verify to the same checksum; a stale or tampered cache never gets replaced
    silently.
    """
    cache_root = Path(cache_root)
    archive = Path(archive)
    expected_sha256 = str(expected_sha256).lower()
    if not re.fullmatch(r'[0-9a-f]{64}', expected_sha256):
        raise RuntimeError('Invalid source-cache SHA-256')
    if not cache_root.is_absolute():
        raise RuntimeError('Conan source cache must be an absolute path')
    if archive.is_symlink() or not archive.is_file():
        raise RuntimeError('Missing verified source archive: ' + str(archive))
    actual_sha256 = sha256(archive)
    if actual_sha256 != expected_sha256:
        raise RuntimeError('Source archive SHA-256 mismatch: ' + archive.name)

    _verified_cache_directory(cache_root, 'Conan source cache')
    source_folder = _verified_cache_directory(cache_root / 's', 'Conan source cache')
    destination = source_folder / expected_sha256
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_file() or sha256(destination) != expected_sha256:
            raise RuntimeError('Existing Conan source-cache entry has a mismatched SHA-256')
        return {'sha256': expected_sha256, 'cache_entry': 's/' + expected_sha256, 'reused': True}

    created = False
    try:
        with archive.open('rb') as source, destination.open('xb') as target:
            created = True
            shutil.copyfileobj(source, target)
            target.flush()
            os.fsync(target.fileno())
    except Exception:
        # The destination is an explicit cache entry owned by this helper.  A
        # partial file is unsafe because Conan treats its presence as a hit.
        if created:
            try:
                destination.unlink()
            except FileNotFoundError:
                pass
        raise
    if sha256(destination) != expected_sha256:
        destination.unlink()
        raise RuntimeError('Conan source-cache entry failed SHA-256 verification')
    return {'sha256': expected_sha256, 'cache_entry': 's/' + expected_sha256, 'reused': False}


def _verify_cached_source(cache_root, expected_sha256):
    cache_root = Path(cache_root)
    if not cache_root.is_absolute():
        raise RuntimeError('Conan source cache must be an absolute path')
    entry = cache_root / 's' / expected_sha256
    if entry.is_symlink() or not entry.is_file() or sha256(entry) != expected_sha256:
        raise RuntimeError('Missing or mismatched verified Conan source-cache entry')


def preflight(graph_path, output, client_preset=None, source_cache=None):
    if output.exists():
        raise RuntimeError('Preflight output exists; refusing to replace prior evidence')
    graph = json.loads(graph_path.read_text())
    nodes = graph.get('graph', {}).get('nodes')
    if not isinstance(nodes, dict) or not nodes:
        raise RuntimeError('Expected complete graph.nodes')
    if source_cache is not None:
        source_cache = Path(source_cache)
        references = {str(node.get('ref', '')).split('#', 1)[0] for node in nodes.values()}
        if DAV1D_SOURCE_REFERENCE in references:
            _verify_cached_source(source_cache, DAV1D_SOURCE_SHA256)
    output.mkdir(parents=True)
    report = {'schema_version': 1, 'graph_sha256': sha256(graph_path), 'pass': False,
              'aborted': False,
              'scope': 'Resolved graph coverage and exact source/notices; partial archives are not publishable',
              'entries': []}
    ci_provenance = verified_github_ci_provenance()
    report['ci_service_path_provenance'] = ci_provenance
    qt_exclusion = None
    if client_preset is not None:
        presets = json.loads(client_preset.read_text()).get('configurePresets', [])
        for preset in presets:
            if preset.get('name') == 'new-horizons-windows-x64':
                values = preset.get('cacheVariables', {})
                if all(values.get(key) == 'OFF' for key in ('ENABLE_LAUNCHER', 'ENABLE_EDITOR')):
                    qt_exclusion = {'preset': preset['name'], 'preset_file_sha256': sha256(client_preset),
                                    'ENABLE_LAUNCHER': 'OFF', 'ENABLE_EDITOR': 'OFF'}
    aborted = False
    for index, (node_id, node) in enumerate(nodes.items()):
        reference = node.get('ref')
        entry = {'node_id': str(node_id), 'reference': reference, 'status': 'not-run',
                 'context': node.get('context'), 'binary': node.get('binary'),
                 'package_id': node.get('package_id'), 'package_revision': node.get('prev')}
        report['entries'].append(entry)
        if aborted:
            continue
        if str(node_id) == '0' or node.get('recipe') == 'Consumer' or not reference:
            entry.update(status='excluded', reason='consumer-or-no-reference')
            continue
        if node.get('context') == 'build':
            entry.update(status='excluded', reason='build-tool-not-shipped-implementation')
            continue
        if reference.startswith('qt/'):
            if qt_exclusion is None:
                entry.update(status='failed', category='unverified-Qt-exclusion')
            else:
                entry.update(status='excluded', reason='Qt tool UI disabled in exact curated preset; transitive host nodes still audited', evidence=qt_exclusion)
            continue
        destination = output / ('dependency-' + str(index))
        destination.mkdir()
        subset = destination / 'private-node-input.json'
        # Paths are private collection inputs, not retained in the report/artifact.
        write_json(subset, {'graph': {'nodes': {'1': node}}})
        try:
            collect_notices(subset, destination / 'notices', destination / 'sources.tar.gz',
                            source_cache=source_cache)
            entry.update(status='passed', artifact_directory=destination.name)
        except RuntimeError as error:
            category = next((value for prefix, value in SAFE_FAILURES.items() if str(error).startswith(prefix)), None)
            entry.update(status='failed', category=category or 'unsafe-or-unclassified-source-failure')
            if category is None:
                aborted = True
        except Exception:
            entry.update(status='failed', category='source-command-or-integrity-failure')
            aborted = True
        finally:
            subset.unlink()
        if not aborted:
            folder = Path(node['package_folder']) if node.get('package_folder') else None
            entry['cached_pe_privacy'] = {'checked': 0, 'findings': [], 'reported_ci_service_paths': []}
            if folder is not None and folder.is_dir():
                try:
                    for binary in sorted(folder.rglob('*.dll')):
                        if not binary.is_file():
                            continue
                        data = binary.read_bytes()
                        if not data.startswith(b'MZ'):
                            continue  # Import archives are not runtime PEs; deployment is a separate gate.
                        entry['cached_pe_privacy']['checked'] += 1
                        hits = inspect_bytes(data, verified_ci=ci_provenance is not None)
                        for classification, key in (('unapproved-profile-path', 'findings'), ('ci-service-build-path', 'reported_ci_service_paths')):
                            selected = [hit for hit in hits if hit['classification'] == classification]
                            if selected:
                                entry['cached_pe_privacy'][key].append({'file': binary.relative_to(folder).as_posix(), 'binary_sha256': sha256(binary), 'hits': selected})
                    if entry['cached_pe_privacy']['findings']:
                        entry['source_notice_status'] = entry['status']
                        entry['status'] = 'failed'
                        entry['binary_privacy_category'] = 'personal-path-in-cached-runtime'
                except Exception:
                    entry.update(status='failed', category='cached-binary-read-failure')
                    aborted = True
        report['aborted'] = aborted
        write_json(output / 'PREFLIGHT-REPORT.json', report)
    report['pass'] = not aborted and not any(entry['status'] == 'failed' for entry in report['entries'])
    report['checked_host_nodes'] = sum(entry['status'] == 'passed' for entry in report['entries'])
    report['failed_nodes'] = sum(entry['status'] == 'failed' for entry in report['entries'])
    if report['checked_host_nodes'] == 0:
        report['pass'] = False
    write_json(output / 'PREFLIGHT-REPORT.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--conan-graph', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--client-preset', type=Path)
    parser.add_argument('--source-cache', type=Path,
                        help='Absolute Conan source backup cache containing verified fallback blobs')
    args = parser.parse_args()
    report = preflight(args.conan_graph, args.output_dir, args.client_preset, args.source_cache)
    print('Source preflight:', report['checked_host_nodes'], 'passed,', report['failed_nodes'], 'failed; aborted=', report['aborted'])
    raise SystemExit(0 if report['pass'] else 1)


if __name__ == '__main__':
    main()
