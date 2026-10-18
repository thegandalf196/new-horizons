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

    def test_inherited_installer_crlf_blobs_survive_late_attributes_and_checkout(self):
        patterns = ('/CI/wininstaller/build_installer.cmd',
                    '/CI/wininstaller/installer.iss', '/CI/wininstaller/lang/*.isl')
        payloads = {'CI/wininstaller/build_installer.cmd': b'echo installer\r\n',
                    'CI/wininstaller/installer.iss': b'[Setup]\r\nAppName=Example\r\n',
                    'CI/wininstaller/lang/example.isl': b'[Messages]\r\nWelcome=Example\r\n'}
        auto_rules = ''.join(pattern + ' text=auto eol=crlf\n' for pattern in patterns).encode()
        forced_rules = auto_rules.replace(b'text=auto', b'text')
        commit = lambda: self.git('-c', 'user.name=Line-ending regression',
                                  '-c', 'user.email=tests@invalid',
                                  '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'fixture')
        for name, raw in payloads.items():
            self.tracked(name, raw)
        commit()  # Upstream CRLF blobs predate the attributes being tested.
        for name, raw in payloads.items():
            self.assertEqual(self.git('show', 'HEAD:' + name), raw)

        def fresh_checkout(label):
            clone = self.root / label
            self.git('clone', '-q', '--no-local', '--no-checkout', str(self.root), str(clone))
            subprocess.check_call(['/usr/bin/git', '-C', str(clone), 'config', 'core.autocrlf', 'false'])
            subprocess.check_call(['/usr/bin/git', '-C', str(clone), '-c', 'core.hooksPath=/dev/null',
                                   'checkout', '-q', '--force', 'HEAD'])
            return clone

        self.tracked('.gitattributes', forced_rules)
        commit()
        forced = fresh_checkout('forced-checkout')
        dirty = subprocess.check_output(['/usr/bin/git', '-C', str(forced), 'status', '--porcelain'])
        self.assertEqual(set(dirty.decode().splitlines()), {' M ' + name for name in payloads})
        for name, raw in payloads.items():
            self.assertEqual((forced / name).read_bytes(), raw)

        self.tracked('.gitattributes', auto_rules)
        commit()
        automatic = fresh_checkout('auto-checkout')
        self.assertEqual(subprocess.check_output(
            ['/usr/bin/git', '-C', str(automatic), 'status', '--porcelain']), b'')
        self.assertFalse(CHECKER.check(automatic)['violations'])
        for name, raw in payloads.items():
            with self.subTest(path=name):
                self.assertEqual((automatic / name).read_bytes(), raw)
                self.assertEqual(self.git('show', 'HEAD:' + name), raw)
                (automatic / name).write_bytes(raw.replace(b'\r\n', b'\n'))
                self.assertEqual(CHECKER.check(automatic)['violations'],
                                 [{'path': name, 'errors': ['bare LF in explicit CRLF text']}])
                (automatic / name).write_bytes(raw)
        actual_rules = [line for line in (ROOT / '.gitattributes').read_text().splitlines()
                        if any(line.startswith(pattern + ' ') for pattern in patterns)]
        self.assertEqual(actual_rules, auto_rules.decode().splitlines())

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
