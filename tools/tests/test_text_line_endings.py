"""Small indexed repositories; no formatting or application execution."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('text_endings', ROOT / 'CI/check_text_line_endings.py')
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


class TextLineEndingsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'core.autocrlf', 'false')

    def git(self, *args):
        return subprocess.check_output(['/usr/bin/git', '-C', str(self.root), *args], stderr=subprocess.PIPE)

    def tracked(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.git('add', '--', name)

    def test_lf_source_passes(self):
        self.tracked('source.cpp', b'int value;\n')
        result = CHECKER.check(self.root)
        self.assertEqual(result['checkedText'], 1)
        self.assertFalse(result['violations'])

    def test_crlf_source_refuses_even_with_nul(self):
        self.tracked('source.cpp', b'int value;\r\n\0')
        result = CHECKER.check(self.root)
        self.assertIn('CRLF in LF text', result['violations'][0]['errors'])
        self.assertEqual(result['checkedText'], 1)

    def test_explicit_windows_crlf_is_allowed_but_lf_refuses(self):
        self.tracked('.gitattributes', b'*.bat text eol=crlf\n')
        self.tracked('windows.bat', b'echo okay\r\n')
        self.assertFalse(CHECKER.check(self.root)['violations'])
        (self.root / 'windows.bat').write_bytes(b'echo wrong\n')
        self.assertTrue(CHECKER.check(self.root)['violations'])

    def test_pinned_minus_text_is_preserved(self):
        self.tracked('.gitattributes', b'/dependencies/** -text\n')
        self.tracked('dependencies/recipe.cpp', b'pinned\r\nbytes\0')
        result = CHECKER.check(self.root)
        self.assertFalse(result['violations'])
        self.assertEqual(result['skippedByScope']['attribute--text'], 1)

    def test_binary_formats_with_crlf_are_not_text(self):
        for name, data in [('image.png', b'\x89PNG\r\n\x1a\n'),
                           ('pack.nhart', b'NHART\r\npayload'),
                           ('design.docx', b'PK\x03\x04\r\ncompressed')]:
            self.tracked(name, data)
        result = CHECKER.check(self.root)
        self.assertEqual(result['checkedText'], 0)
        self.assertEqual(result['skippedByScope']['known-binary'], 3)
        self.assertFalse(result['violations'])

    def test_untracked_and_ignored_inputs_are_not_read(self):
        self.tracked('.gitignore', b'private/\n')
        (self.root / 'untracked.cpp').write_bytes(b'untracked\r\n')
        (self.root / 'private').mkdir()
        (self.root / 'private/save.txt').write_bytes(b'private\r\n')
        result = CHECKER.check(self.root)
        self.assertEqual(result['trackedEntries'], 1)
        self.assertFalse(result['violations'])

    def test_unknown_format_is_reported_not_guessed_from_nul(self):
        self.tracked('unknown.payload', b'unknown\r\n')
        result = CHECKER.check(self.root)
        self.assertEqual(result['skippedByScope'], {'unknown-format': 1})

    def test_explicit_text_unknown_extension_is_checked(self):
        self.tracked('.gitattributes', b'*.custom text\n')
        self.tracked('source.custom', b'wrong\r\n')
        self.assertTrue(CHECKER.check(self.root)['violations'])

    def test_symlink_does_not_follow_external_text(self):
        (self.root / 'external').write_bytes(b'bad\r\n')
        (self.root / 'link.cpp').symlink_to(self.root / 'external')
        self.git('add', 'link.cpp')
        result = CHECKER.check(self.root)
        self.assertEqual(result['skippedByScope'], {'symlink-or-submodule': 1})

    def test_replaced_tracked_parent_does_not_follow_untracked_directory(self):
        self.tracked('source/file.cpp', b'good\n')
        (self.root / 'source/file.cpp').unlink()
        (self.root / 'source').rmdir()
        (self.root / 'private').mkdir()
        (self.root / 'private/file.cpp').write_bytes(b'untracked\r\n')
        (self.root / 'source').symlink_to(self.root / 'private', target_is_directory=True)
        self.assertEqual(CHECKER.check(self.root)['violations'][0]['errors'],
                         ['missing or nonregular tracked text'])

    def test_declared_utf16_is_decoded_before_line_check(self):
        self.tracked('.gitattributes', b'*.txt text working-tree-encoding=UTF-16LE eol=lf\n')
        self.tracked('wide.txt', 'hello\n'.encode('utf-16-le'))
        self.assertFalse(CHECKER.check(self.root)['violations'])
        (self.root / 'wide.txt').write_bytes('hello\r\n'.encode('utf-16-le'))
        self.assertIn('CRLF in LF text', CHECKER.check(self.root)['violations'][0]['errors'])

    def test_bare_cr_refuses(self):
        self.tracked('source.py', b'one\rtwo\n')
        self.assertIn('bare CR', CHECKER.check(self.root)['violations'][0]['errors'])


if __name__ == '__main__':
    unittest.main()
