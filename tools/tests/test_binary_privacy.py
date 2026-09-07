#!/usr/bin/env python3
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from binary_privacy import inspect_bytes, require_clean


class BinaryPrivacyTest(unittest.TestCase):
    def test_ascii_data_section_paths_are_not_treated_as_debug_only(self):
        data = b'MZsynthetic\0.rdata\0/home/synthetic-builder/source.cpp\0'
        hits = inspect_bytes(data)
        self.assertTrue(any(h['category'] == 'unix-home' for h in hits))
        self.assertNotIn('synthetic-builder', str(hits))

    def test_utf16_at_both_alignments_and_supplementary_prefix(self):
        text = '\U0001f600 C:\\Users\\synthetic-builder\\source.cpp'
        for prefix in (b'', b'x'):
            data = prefix + text.encode('utf-16-le')
            hits = inspect_bytes(data)
            wanted = len(prefix) + len('\U0001f600 '.encode('utf-16-le'))
            self.assertTrue(any(h['category'] == 'windows-profile' and h['offset'] == wanted for h in hits))

    def test_hosted_workspace_and_relative_source_paths_are_not_profiles(self):
        self.assertEqual(inspect_bytes(b'D:\\a\\project\\source.cpp\0./lib/source.cpp\0'), [])

    def test_binary_gate_rejects_runtime_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'sample.dll').write_bytes(b'MZ\0/home/synthetic-builder/a.cpp\0')
            with self.assertRaisesRegex(RuntimeError, 'do not publish'):
                require_clean(root)


if __name__ == '__main__':
    unittest.main()
