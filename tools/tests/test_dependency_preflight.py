#!/usr/bin/env python3
"""Independent safe errors aggregate; unsafe source work must stop immediately."""
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

    def execute(self, side_effect, client_preset=None):
        self.graph.write_text(json.dumps({'graph': {'nodes': self.nodes}}))
        with patch.object(gate, 'collect_notices', side_effect=side_effect) as collect:
            report = gate.preflight(self.graph, self.root / 'result', client_preset)
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


if __name__ == '__main__':
    unittest.main()
