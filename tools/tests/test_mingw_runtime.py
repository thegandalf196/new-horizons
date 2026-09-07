#!/usr/bin/env python3
"""Synthetic closure/alias negative controls; actual PE execution remains separate."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import mingw_runtime as runtime


class MinGWRuntimeTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        for name in ('VCMI_client.exe', 'VCMI_lib.dll'):
            (self.root / name).write_bytes(name.encode())
        self.image = {'machine': 'AMD64', 'subsystem': 3, 'imports': [], 'forwarders': [],
                      'export_names': {'entry'}, 'export_ordinals': {1}, 'export_dll_name': 'vcmi_lib.dll'}

    def inspect(self, path):
        result = copy.deepcopy(self.image)
        if path.suffix == '.exe':
            result['imports'] = [{'dll': 'vcmi_lib.dll', 'name': 'entry', 'ordinal': None, 'delay': True}]
        return result

    def test_resolves_bundled_delay_import_and_checks_export(self):
        with patch.object(runtime, 'inspect_pe', side_effect=self.inspect):
            report = runtime.audit_directory(self.root)
        self.assertEqual(report['vcmi_client.exe']['checked_imports_and_forwarders'], 1)
        self.assertIn('requested exports present', report['vcmi_client.exe']['dependencies']['vcmi_lib.dll'])

    def test_missing_export_rejected(self):
        self.image['export_names'] = set()
        with patch.object(runtime, 'inspect_pe', side_effect=self.inspect), self.assertRaisesRegex(RuntimeError, 'Missing PE export'):
            runtime.audit_directory(self.root)

    def test_unresolved_forwarder_rejected(self):
        self.image['forwarders'] = [{'dll': 'absent.dll', 'name': 'entry', 'ordinal': None}]
        with patch.object(runtime, 'inspect_pe', side_effect=self.inspect), self.assertRaisesRegex(RuntimeError, 'Unresolved non-system'):
            runtime.audit_directory(self.root)

    def test_case_collision_rejected(self):
        (self.root / 'VCMI_LIB.DLL').write_bytes(b'collision')
        with self.assertRaisesRegex(RuntimeError, 'collision'):
            runtime.audit_directory(self.root)

    def test_ogg_alias_preserves_original_bytes(self):
        original = self.root / 'libogg.dll'
        original.write_bytes(b'synthetic exact cached payload')
        before = original.read_bytes()
        with patch.object(runtime, 'inspect_pe', return_value={'export_dll_name': 'ogg.dll'}):
            record = runtime.stage_ogg_loader_alias(self.root)
        self.assertEqual(original.read_bytes(), before)
        self.assertEqual((self.root / 'ogg.dll').read_bytes(), before)
        self.assertEqual(record['loader_alias'], 'ogg.dll')

    def test_alias_requires_exact_export_identity(self):
        (self.root / 'libogg.dll').write_bytes(b'wrong library')
        with patch.object(runtime, 'inspect_pe', return_value={'export_dll_name': 'other.dll'}), self.assertRaisesRegex(RuntimeError, 'identity'):
            runtime.stage_ogg_loader_alias(self.root)
        self.assertFalse((self.root / 'ogg.dll').exists())


if __name__ == '__main__':
    unittest.main()
