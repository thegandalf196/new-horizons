"""Committed-manifest native pin preparation and fail-closed CI wiring."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import prepare_nhart_native_pin as pin


class NativePinTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name); self.repo = self.root / 'repo'; self.repo.mkdir()
        inputs = self.root / 'inputs'; inputs.mkdir(); (inputs / 'tiny.pcx').write_bytes(b'Synthetic selected payload')
        manifest = {'format': 1, 'requiredFamilies': ['builtin-skills-perks-orders-and-ui'],
            'requiredFamilyCounts': {'builtin-skills-perks-orders-and-ui': 1}, 'entries': [{
                'resource': 'SPRITES/tiny.pcx', 'source': 'tiny.pcx', 'size': (inputs / 'tiny.pcx').stat().st_size,
                'sha256': pin.sha256(inputs / 'tiny.pcx'), 'family': 'builtin-skills-perks-orders-and-ui',
                'origin': 'synthetic-test', 'selection': 'test', 'approval': 'test'}]}
        manifest_path = self.repo / pin.MANIFEST; manifest_path.parent.mkdir(parents=True)
        manifest_path.write_text(json.dumps(manifest))
        pack = self.repo / pin.PACK; pack.parent.mkdir(parents=True); pin.nhart.pack(manifest, inputs, pack)
        self.installed = self.root / 'installed.nhart'; self.installed.write_bytes(pack.read_bytes())
        self.committed = {pin.PACK: pack.read_bytes(), pin.MANIFEST: manifest_path.read_bytes()}
        self.mock = patch.object(pin, 'committed_bytes', side_effect=lambda repo, name: self.committed[name])
        self.mock.start(); self.addCleanup(self.mock.stop)

    def test_exact_committed_manifest_and_pack_produce_safe_encoded_arguments(self):
        result = pin.verified_pin(self.repo, self.installed)
        self.assertEqual(result, (hashlib.sha256(self.committed[pin.PACK]).hexdigest(), 1))
        encoded = pin.encoded_arguments(result)
        self.assertEqual(encoded.split(b'\0'), [b'--expected-pack-sha256', result[0].encode(), b'--expected-resources', b'1', b''])

    def test_installed_pack_must_match_committed_bytes(self):
        self.installed.write_bytes(self.installed.read_bytes() + b'wrong revision')
        with self.assertRaisesRegex(ValueError, 'Installed pack'): pin.verified_pin(self.repo, self.installed)

    def test_checkout_manifest_must_be_exact_committed_selection(self):
        path = self.repo / pin.MANIFEST; manifest = json.loads(path.read_bytes())
        manifest['entries'][0]['approval'] = 'uncommitted selection'
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'Selected manifest'): pin.verified_pin(self.repo, self.installed)

    def test_checkout_crlf_whitespace_and_key_order_preserve_selection(self):
        path = self.repo / pin.MANIFEST; manifest = json.loads(path.read_bytes())
        path.write_bytes((json.dumps(manifest, sort_keys=True, indent=2) + '\n').replace('\n', '\r\n').encode())
        self.assertEqual(pin.verified_pin(self.repo, self.installed),
                         (hashlib.sha256(self.committed[pin.PACK]).hexdigest(), 1))

    def test_checkout_scalar_type_change_is_not_line_ending_normalization(self):
        path = self.repo / pin.MANIFEST; manifest = json.loads(path.read_bytes())
        manifest['format'] = True
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'Selected manifest'): pin.verified_pin(self.repo, self.installed)

    def test_checkout_pack_cannot_self_pin_dirty_bytes(self):
        path = self.repo / pin.PACK; path.write_bytes(path.read_bytes() + b'dirty')
        with self.assertRaisesRegex(ValueError, 'Checkout pack'): pin.verified_pin(self.repo, self.installed)

    def test_manifest_and_embedded_provenance_must_agree(self):
        manifest = json.loads(self.committed[pin.MANIFEST]); manifest['entries'][0]['approval'] = 'different selection'
        changed = json.dumps(manifest).encode(); self.committed[pin.MANIFEST] = changed
        (self.repo / pin.MANIFEST).write_bytes(changed)
        with self.assertRaisesRegex(pin.nhart.NHArtError, 'Embedded manifest differs'): pin.verified_pin(self.repo, self.installed)

    def test_argument_encoding_rejects_malformed_pins_and_shell_text(self):
        for digest in ('', 'a' * 63, 'a' * 65, 'G' * 64, 'a' * 63 + ';', '$(command)'):
            with self.subTest(digest=digest), self.assertRaises(ValueError): pin.encoded_arguments((digest, 1))
        for count in (0, -1, 1.5, True, '1', pin.nhart.MAX_ENTRIES, 10 ** 100):
            with self.subTest(count=count), self.assertRaises(ValueError): pin.encoded_arguments(('a' * 64, count))

    def test_current_committed_pack_is_not_frozen_to_historical_count(self):
        self.mock.stop()
        actual = pin.verified_pin(ROOT, ROOT / pin.PACK)
        selected = pin.nhart.verify(ROOT / pin.PACK, ROOT / pin.MANIFEST)
        self.assertEqual(actual[0], pin.sha256(ROOT / pin.PACK))
        self.assertEqual(actual[1], len(selected['manifest']['entries']))

    def test_native_probe_retains_strict_checks_and_workflow_safe_array(self):
        native = (ROOT / 'client/tests/NhArtStartupRuntimeTest.cpp').read_text()
        workflow = (ROOT / '.github/workflows/new-horizons-windows.yml').read_text()
        self.assertNotIn('45583f1f', native); self.assertNotIn('EXPECTED_RESOURCES = 3137', native)
        for required in ('argc != 9', 'expected.sha256.size() == 64', 'std::from_chars',
                         'parsedCount.ptr == countArgument.data() + countArgument.size()',
                         'expected.resources > 0 && expected.resources < 1000000',
                         '== expected.sha256', 'resources.size() == expected.resources',
                         'files == expected.resources + 1', 'cached == read(direct, resource)',
                         'Warm-loop payload hashes differ', 'no caller paths deleted'):
            self.assertIn(required, native)
        start = workflow.index('- name: Verify native NHART preparation and matched-byte reads (CPU only)')
        end = workflow.index('- name: Package client', start); step = workflow[start:end]
        self.assertIn('python tools/ci/prepare_nhart_native_pin.py --repo .', step)
        self.assertIn("mapfile -d '' -t pin_args", step)
        self.assertIn('"${pin_args[@]}"', step)
        self.assertNotIn('eval ', step)


if __name__ == '__main__':
    unittest.main()
