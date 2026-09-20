#!/usr/bin/env python3
"""Independent safe errors aggregate; unsafe source work must stop immediately."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import preflight_dependency_sources as gate


class DependencyPreflightTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.graph = self.root / 'graph.json'
        self.nodes = {str(i): {'ref': f'library{i}/1#pinned', 'context': 'host', 'binary': 'Skip', 'options': {'header_only': True}}
                      for i in range(1, 4)}

    def execute(self, side_effect, client_preset=None, source_cache=None):
        self.graph.write_text(json.dumps({'graph': {'nodes': self.nodes}}))
        with patch.object(gate, 'collect_notices', side_effect=side_effect) as collect:
            report = gate.preflight(self.graph, self.root / 'result', client_preset, source_cache)
        return report, collect.call_count

    def test_all_safe_missing_notices_are_reported_in_one_run(self):
        report, count = self.execute([RuntimeError('Missing dependency license texts: library1'),
                                      RuntimeError('Missing verified SQLite upstream public-domain notice'), None])
        self.assertEqual(count, 3)
        self.assertEqual(report['failed_nodes'], 2)
        self.assertEqual(report['checked_host_nodes'], 1)
        self.assertFalse(report['pass'])
        self.assertFalse(report['aborted'])
        self.assertTrue((self.root / 'result/PREFLIGHT-REPORT.json').is_file())
        self.assertFalse(list((self.root / 'result').rglob('private-node-input.json')))

    def test_notice_and_cached_binary_privacy_failures_are_both_retained(self):
        folder = self.root / 'cached'
        folder.mkdir()
        (folder / 'runtime.dll').write_bytes(b'MZ\x00/home/synthetic-builder/source.cpp\x00')
        self.nodes['1']['package_folder'] = str(folder)
        report, count = self.execute([RuntimeError('Missing dependency license texts: library1'), None, None])
        self.assertEqual(count, 3)
        entry = report['entries'][0]
        self.assertEqual(entry['category'], 'missing-notice')
        self.assertEqual(entry['binary_privacy_category'], 'personal-path-in-cached-runtime')
        self.assertEqual(len(entry['cached_pe_privacy']['findings'][0]['hits']), 1)
        self.assertFalse(report['pass'])

    def test_unsafe_extraction_aborts_before_next_dependency(self):
        report, count = self.execute(RuntimeError('Escaping archive link: synthetic'))
        self.assertEqual(count, 1)
        self.assertTrue(report['aborted'])
        self.assertFalse(report['pass'])
        self.assertEqual([e['status'] for e in report['entries']], ['failed', 'not-run', 'not-run'])

    def test_all_static_header_only_host_nodes_are_checked(self):
        self.nodes['tool'] = {'ref': 'generator/1', 'context': 'build'}
        report, count = self.execute(None)
        self.assertEqual(count, 3)
        self.assertTrue(report['pass'])
        self.assertEqual(report['entries'][-1]['reason'], 'build-tool-not-shipped-implementation')

    def test_unclassified_failure_aborts_fail_closed(self):
        report, count = self.execute(ValueError('Untrusted malformed source archive'))
        self.assertEqual(count, 1)
        self.assertTrue(report['aborted'])
        self.assertFalse(report['pass'])

    def test_qt_exclusion_without_preset_evidence_fails(self):
        self.nodes['qt'] = {'ref': 'qt/6', 'context': 'host'}
        report, count = self.execute(None)
        self.assertEqual(count, 3)
        self.assertFalse(report['pass'])
        self.assertEqual(report['entries'][-1]['category'], 'unverified-Qt-exclusion')

    def test_qt_exclusion_records_exact_disabled_tool_preset(self):
        self.nodes['qt'] = {'ref': 'qt/6', 'context': 'host'}
        preset = self.root / 'CMakePresets.json'
        preset.write_text(json.dumps({'configurePresets': [{'name': 'new-horizons-windows-x64',
                                      'cacheVariables': {'ENABLE_LAUNCHER': 'OFF', 'ENABLE_EDITOR': 'OFF'}}]}))
        report, count = self.execute(None, preset)
        self.assertEqual(count, 3)
        self.assertTrue(report['pass'])
        self.assertEqual(report['entries'][-1]['evidence']['ENABLE_EDITOR'], 'OFF')

    def test_no_implementation_checked_is_not_success(self):
        self.nodes = {'0': {'recipe': 'Consumer'}}
        report, count = self.execute(None)
        self.assertEqual(count, 0)
        self.assertFalse(report['pass'])

    def test_seed_source_cache_verifies_and_is_idempotent(self):
        archive = self.root / 'dav1d-1.5.4.tar.xz'
        archive.write_bytes(b'verified source fixture')
        expected = hashlib.sha256(archive.read_bytes()).hexdigest()
        cache = self.root / 'source-cache'

        first = gate.seed_source_cache(cache, archive, expected)
        second = gate.seed_source_cache(cache, archive, expected)
        entry = cache / 's' / expected
        self.assertFalse(first['reused'])
        self.assertTrue(second['reused'])
        self.assertEqual(entry.read_bytes(), archive.read_bytes())

    def test_seed_source_cache_rejects_mismatch_and_tampered_existing_entry(self):
        archive = self.root / 'dav1d-1.5.4.tar.xz'
        archive.write_bytes(b'verified source fixture')
        expected = hashlib.sha256(archive.read_bytes()).hexdigest()
        cache = self.root / 'source-cache'

        with self.assertRaisesRegex(RuntimeError, 'SHA-256 mismatch'):
            gate.seed_source_cache(cache, archive, '0' * 64)

        gate.seed_source_cache(cache, archive, expected)
        (cache / 's' / expected).write_bytes(b'tampered')
        with self.assertRaisesRegex(RuntimeError, 'mismatched SHA-256'):
            gate.seed_source_cache(cache, archive, expected)

    def test_dav1d_preflight_requires_and_passes_verified_source_cache(self):
        self.nodes = {'1': {'ref': gate.DAV1D_SOURCE_REFERENCE + '#pinned',
                            'context': 'host', 'binary': 'Skip'}}
        archive = self.root / 'dav1d-1.5.4.tar.xz'
        archive.write_bytes(b'fixture matching pinned fallback')
        # The real pinned hash is intentionally checked by the helper; use a
        # direct cache fixture here so this unit test remains independent of
        # the release archive bytes.
        cache = self.root / 'source-cache'
        (cache / 's').mkdir(parents=True)
        (cache / 's' / gate.DAV1D_SOURCE_SHA256).write_bytes(b'wrong fixture')
        with self.assertRaisesRegex(RuntimeError, 'Missing or mismatched verified'):
            self.execute(None, source_cache=cache)

        valid = b'locally verified dav1d source fixture'
        expected = hashlib.sha256(valid).hexdigest()
        (cache / 's' / expected).write_bytes(valid)
        with patch.object(gate, 'DAV1D_SOURCE_SHA256', expected):
            report, count = self.execute(None, source_cache=cache)
        self.assertTrue(report['pass'])
        self.assertEqual(count, 1)


if __name__ == '__main__':
    unittest.main()
