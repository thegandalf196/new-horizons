#!/usr/bin/env python3
"""Collect independent, safe notice/source failures across the resolved host graph.

Partial output is never publishable. Unsafe extraction or an unclassified source
command failure aborts immediately, with a structured failure report retained.
"""
import argparse
import json
from pathlib import Path

from package_new_horizons_windows import collect_notices, sha256, write_json
from binary_privacy import inspect_bytes, verified_github_ci_provenance

SAFE_FAILURES = {
    'Missing dependency license texts': 'missing-notice',
    'Missing verified SQLite upstream public-domain notice': 'missing-sqlite-notice',
    'Missing FFmpeg source license text': 'missing-ffmpeg-terms',
    'Missing exact cached recipe for dependency': 'missing-recipe',
    'Missing or mismatched exact exported sources': 'missing-verified-exports',
}


def preflight(graph_path, output, client_preset=None):
    if output.exists():
        raise RuntimeError('Preflight output exists; refusing to replace prior evidence')
    graph = json.loads(graph_path.read_text())
    nodes = graph.get('graph', {}).get('nodes')
    if not isinstance(nodes, dict) or not nodes:
        raise RuntimeError('Expected complete graph.nodes')
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
            collect_notices(subset, destination / 'notices', destination / 'sources.tar.gz')
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
    args = parser.parse_args()
    report = preflight(args.conan_graph, args.output_dir, args.client_preset)
    print('Source preflight:', report['checked_host_nodes'], 'passed,', report['failed_nodes'], 'failed; aborted=', report['aborted'])
    raise SystemExit(0 if report['pass'] else 1)


if __name__ == '__main__':
    main()
