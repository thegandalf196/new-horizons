"""Synthetic indexed JSONC fixtures; no game, data archive or build required."""
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('validate_json', ROOT / 'CI/validate_json.py')
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class TrackedJsonTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'core.autocrlf', 'false')

    def git(self, *args, input_bytes=None):
        return subprocess.check_output(
            ['/usr/bin/git', '-C', str(self.root), *args],
            input=input_bytes, stderr=subprocess.PIPE)

    def tracked(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.git('add', '--', name)
        return path

    def test_valid_jsonc_preserves_comments_trailing_commas_and_strings(self):
        self.tracked('config/valid.json', b'''{
            // Existing JSONC semantics, not JSON5.
            "url": "https://example.invalid/a//b", /* comment */
            "values": [1, 2,],
        }\n''')
        self.assertEqual(VALIDATOR.check(self.root), {
            'trackedJson': 1, 'validJson': 1, 'failedJson': 0, 'errors': []})

    def test_invalid_json_and_json5_are_counted_without_stopping_scan(self):
        self.tracked('good.json', b'{"okay": true}\n')
        self.tracked('bad.json', b'{"unfinished": }\n')
        self.tracked('json5.json', b"{unquoted: 'not JSONC'}\n")
        result = VALIDATOR.check(self.root)
        self.assertEqual((result['trackedJson'], result['validJson'], result['failedJson']), (3, 1, 2))
        self.assertEqual([row['path'] for row in result['errors']], ['bad.json', 'json5.json'])
        self.assertTrue(all('line 1, column' in row['error'] for row in result['errors']))

    def test_untracked_ignored_and_non_json_inputs_are_not_opened(self):
        self.tracked('.gitignore', b'build/\nprivate/\n')
        self.tracked('config/current.json', b'{}\n')
        self.tracked('source.cpp', b'not JSON\n')
        for name in ('untracked.json', 'build/cache.json', 'private/input.json'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'NOT JSON\n')
        original_read = Path.read_bytes

        def only_current(path):
            self.assertEqual(path, self.root / 'config/current.json')
            return original_read(path)

        with patch.object(Path, 'read_bytes', only_current):
            self.assertEqual(VALIDATOR.check(self.root)['validJson'], 1)

    def test_tracked_hidden_json_is_not_silently_excluded(self):
        self.tracked('.public/invalid.json', b'NOT JSON\n')
        result = VALIDATOR.check(self.root)
        self.assertEqual(result['failedJson'], 1)
        self.assertEqual(result['errors'][0]['path'], '.public/invalid.json')

    def test_working_tree_bytes_are_checked_without_rewriting_index_or_file(self):
        path = self.tracked('config/current.json', b'{}\n')
        path.write_bytes(b'broken\n')
        before = self.git('ls-files', '--stage', '-z')
        self.assertEqual(VALIDATOR.check(self.root)['failedJson'], 1)
        self.assertEqual(path.read_bytes(), b'broken\n')
        self.assertEqual(self.git('ls-files', '--stage', '-z'), before)

    def test_missing_or_nonregular_tracked_json_refuses(self):
        path = self.tracked('current.json', b'{}\n')
        path.unlink()
        self.assertEqual(VALIDATOR.check(self.root)['failedJson'], 1)
        path.mkdir()
        self.assertEqual(VALIDATOR.check(self.root)['failedJson'], 1)

    def test_indexed_symlink_does_not_follow_target(self):
        (self.root / 'external').write_bytes(b'{}\n')
        (self.root / 'linked.json').symlink_to(self.root / 'external')
        self.git('add', 'linked.json')
        with patch.object(Path, 'read_bytes', side_effect=AssertionError('must not read target')):
            result = VALIDATOR.check(self.root)
        self.assertEqual(result['failedJson'], 1)
        self.assertIn('symlink', result['errors'][0]['error'])

    def test_replaced_file_or_parent_symlink_refuses_before_read(self):
        path = self.tracked('config/current.json', b'{}\n')
        external = self.root / 'private'
        external.mkdir()
        (external / 'current.json').write_bytes(b'{}\n')
        path.unlink()
        path.symlink_to(external / 'current.json')
        with patch.object(Path, 'read_bytes', side_effect=AssertionError('must not read target')):
            self.assertEqual(VALIDATOR.check(self.root)['failedJson'], 1)
        path.unlink()
        path.parent.rmdir()
        path.parent.symlink_to(external, target_is_directory=True)
        with patch.object(Path, 'read_bytes', side_effect=AssertionError('must not read target')):
            self.assertEqual(VALIDATOR.check(self.root)['failedJson'], 1)

    def test_utf8_bom_and_invalid_encoding_refuse_without_raw_data(self):
        for name, content in (('invalid.json', b'\xffPRIVATE-CONTENT'),
                              ('bom.json', b'\xef\xbb\xbf{}\n')):
            self.tracked(name, content)
        result = VALIDATOR.check(self.root)
        self.assertEqual(result['failedJson'], 2)
        self.assertIn('invalid UTF-8 at byte 0', result['errors'][1]['error'])
        self.assertNotIn('PRIVATE-CONTENT', json.dumps(result))
        self.assertNotIn(str(self.root), json.dumps(result))

    def test_unmerged_index_refuses_before_any_input_read(self):
        path = self.tracked('conflict.json', b'{}\n')
        blob = self.git('hash-object', str(path)).strip()
        self.git('update-index', '--force-remove', 'conflict.json')
        records = b''.join(b'100644 ' + blob + b' ' + str(stage).encode() + b'\tconflict.json\n'
                           for stage in (1, 2, 3))
        self.git('update-index', '--index-info', input_bytes=records)
        with patch.object(Path, 'read_bytes', side_effect=AssertionError('must not read conflict')):
            with self.assertRaisesRegex(ValueError, 'Unmerged index entry: conflict.json'):
                VALIDATOR.check(self.root)

    def test_repo_argument_must_identify_actual_git_root(self):
        child = self.root / 'child'
        child.mkdir()
        with self.assertRaisesRegex(ValueError, 'actual Git root'):
            VALIDATOR.check(child)
        with tempfile.TemporaryDirectory() as outside:
            with self.assertRaisesRegex(ValueError, 'existing Git checkout'):
                VALIDATOR.check(Path(outside))

    def test_cli_counts_failures_and_returns_nonzero(self):
        self.tracked('good.json', b'{}\n')
        self.tracked('bad.json', b'broken\n')
        output = io.StringIO()
        with redirect_stdout(output):
            code = VALIDATOR.main(['--repo', str(self.root)])
        self.assertEqual(code, 1)
        result = json.loads(output.getvalue())
        self.assertEqual((result['trackedJson'], result['validJson'], result['failedJson']), (2, 1, 1))
        self.assertNotIn(str(self.root), output.getvalue())

    def test_cli_setup_refusal_is_actionable_without_absolute_path(self):
        child = self.root / 'child'
        child.mkdir()
        output = io.StringIO()
        with redirect_stderr(output):
            code = VALIDATOR.main(['--repo', str(child)])
        self.assertEqual(code, 2)
        self.assertIn('actual Git root', json.loads(output.getvalue())['refused'])
        self.assertNotIn(str(self.root), output.getvalue())


if __name__ == '__main__':
    unittest.main()
