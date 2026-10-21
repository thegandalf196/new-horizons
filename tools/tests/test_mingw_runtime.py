#!/usr/bin/env python3
"""Synthetic closure/alias negative controls; actual PE execution remains separate."""
import copy
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import mingw_runtime as runtime


class PEExportInspectionTest(unittest.TestCase):
    def image(self, count=3):
        image = MagicMock()
        image.__enter__.return_value = image
        image.FILE_HEADER.Machine = 0x8664
        image.OPTIONAL_HEADER.Magic = 0x20b
        image.OPTIONAL_HEADER.Subsystem = 3
        image.OPTIONAL_HEADER.DATA_DIRECTORY = [SimpleNamespace(VirtualAddress=0x1000)]
        image.DIRECTORY_ENTRY_IMPORT = []
        image.DIRECTORY_ENTRY_DELAY_IMPORT = []
        image.DIRECTORY_ENTRY_EXPORT = SimpleNamespace(
            name=b'vcmi_lib.dll',
            struct=SimpleNamespace(NumberOfFunctions=count, NumberOfNames=count,
                                   AddressOfFunctions=0x1100, Base=1),
            symbols=[SimpleNamespace(name=('entry' + str(index)).encode(), ordinal=index + 1,
                                     forwarder=None) for index in range(count)])
        image.get_data.return_value = struct.pack('<' + 'I' * count, *range(1, count + 1))
        return image

    def inspect(self, image):
        module = Mock()
        module.PE.return_value = image
        with patch.dict(sys.modules, {'pefile': module}):
            result = runtime.inspect_pe(Path('synthetic.dll'))
        module.PE.assert_called_once_with('synthetic.dll', fast_load=True,
                                         max_symbol_exports=65_536)
        return result

    def test_more_than_default_8192_names_are_fully_available(self):
        result = self.inspect(self.image(8194))
        self.assertEqual(len(result['export_names']), 8194)
        self.assertIn('entry8193', result['export_names'])

    def test_silent_name_truncation_is_rejected_even_with_all_ordinals(self):
        image = self.image(8194)
        for symbol in image.DIRECTORY_ENTRY_EXPORT.symbols[8192:]:
            symbol.name = None  # pefile's default-limit fallback preserves ordinals.
        with self.assertRaisesRegex(RuntimeError, 'Incomplete PE export'):
            self.inspect(image)

    def test_missing_ordinal_is_rejected(self):
        image = self.image()
        image.DIRECTORY_ENTRY_EXPORT.symbols.pop()
        image.DIRECTORY_ENTRY_EXPORT.struct.NumberOfNames -= 1
        with self.assertRaisesRegex(RuntimeError, 'Incomplete PE export'):
            self.inspect(image)

    def test_sparse_ordinals_aliases_and_forwarders_remain_valid(self):
        image = self.image()
        image.get_data.return_value = struct.pack('<III', 0x2000, 0, 0x3000)
        image.DIRECTORY_ENTRY_EXPORT.symbols[1].ordinal = 1  # Named alias.
        image.DIRECTORY_ENTRY_EXPORT.symbols[2].forwarder = b'other.entry'
        result = self.inspect(image)
        self.assertEqual(result['export_ordinals'], {1, 3})
        self.assertEqual(result['forwarders'], [{'dll': 'other.dll', 'name': 'entry',
                                                'ordinal': None, 'delay': False}])

    def test_excessive_declared_counts_are_rejected(self):
        for field in ('NumberOfFunctions', 'NumberOfNames'):
            image = self.image()
            setattr(image.DIRECTORY_ENTRY_EXPORT.struct, field, 65_537)
            with self.subTest(field=field), self.assertRaisesRegex(RuntimeError, 'inspection limit'):
                self.inspect(image)
            image.get_data.assert_not_called()

    def test_truncated_address_table_is_rejected(self):
        image = self.image()
        image.get_data.return_value = b'\x00'
        with self.assertRaisesRegex(RuntimeError, 'Truncated PE export'):
            self.inspect(image)

    def test_unparsed_nonempty_export_directory_is_rejected(self):
        image = self.image()
        del image.DIRECTORY_ENTRY_EXPORT
        with self.assertRaisesRegex(RuntimeError, 'could not be parsed'):
            self.inspect(image)
        image.OPTIONAL_HEADER.DATA_DIRECTORY[0].VirtualAddress = 0
        result = self.inspect(image)
        self.assertEqual(result['export_names'], set())


class MinGWRuntimeTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        for name in ('new-horizons.exe', 'VCMI_lib.dll'):
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
        self.assertEqual(report['new-horizons.exe']['checked_imports_and_forwarders'], 1)
        self.assertIn('requested exports present', report['new-horizons.exe']['dependencies']['vcmi_lib.dll'])

    def test_legacy_requires_explicit_client_argument(self):
        (self.root / 'new-horizons.exe').rename(self.root / 'VCMI_client.exe')
        with patch.object(runtime, 'inspect_pe', side_effect=self.inspect):
            with self.assertRaisesRegex(RuntimeError, 'Unexpected executable identity'):
                runtime.audit_directory(self.root)
            report = runtime.audit_directory(self.root, 'VCMI_client.exe')
        self.assertIn('vcmi_client.exe', report)

    def test_ambiguous_and_invalid_client_identity_rejects_before_pe_parsing(self):
        (self.root / 'VCMI_client.exe').write_bytes(b'legacy fixture')
        with patch.object(runtime, 'inspect_pe', side_effect=AssertionError('Must reject before parsing')) as inspect:
            for client in ('new-horizons.exe', 'VCMI_client.exe', 'other.exe', '../new-horizons.exe'):
                with self.subTest(client=client), self.assertRaises(RuntimeError):
                    runtime.audit_directory(self.root, client)
        inspect.assert_not_called()

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
        # Model an input listing from a case-sensitive build host even when
        # this regression runs on NTFS, which cannot create both directory entries.
        directory = Mock()
        directory.iterdir.return_value = [self.root / 'VCMI_lib.dll', self.root / 'VCMI_LIB.DLL', self.root / 'new-horizons.exe']
        with patch.object(runtime, 'inspect_pe', side_effect=AssertionError('Collision must reject before PE parsing')) as inspect:
            with self.assertRaisesRegex(RuntimeError, 'collision'):
                runtime.audit_directory(directory)
        inspect.assert_not_called()

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
