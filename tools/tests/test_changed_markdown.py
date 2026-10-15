"""Synthetic indexed repositories; selector only, no markdownlint invocation."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('changed_markdown', ROOT / 'CI/changed_markdown.py')
SELECTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SELECTOR)


class ChangedMarkdownTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.git('config', 'core.autocrlf', 'false')
        self.write('README.md', '# Initial\n')
        self.write('source.py', 'value = 1\n')
        self.git('commit', '-qm', 'baseline')
        self.base = self.git('rev-parse', 'HEAD').decode().strip()

    def git(self, *args):
        return subprocess.check_output(['/usr/bin/git', '-C', str(self.root), *args], stderr=subprocess.PIPE)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        self.git('add', '--', name)

    def test_no_docs_changed(self):
        self.write('source.py', 'value = 2\n')
        self.assertEqual(SELECTOR.select(self.root, self.base)['files'], [])

    def test_modified_and_added_space_paths(self):
        self.write('README.md', '# Updated\n')
        self.write('docs/Normal name.md', '# New\n')
        self.assertEqual(SELECTOR.select(self.root, self.base)['files'],
                         ['./README.md', './docs/Normal name.md'])

    def test_untracked_ignored_and_deleted_are_omitted(self):
        self.write('.gitignore', 'private/\n')
        (self.root / 'untracked.md').write_text('not selected')
        (self.root / 'private').mkdir()
        (self.root / 'private/note.md').write_text('not selected')
        self.git('rm', '-q', 'README.md')
        self.assertEqual(SELECTOR.select(self.root, self.base)['count'], 0)

    def test_lint_config_change_selects_all_surviving_tracked_markdown(self):
        self.write('docs/Other.md', '# Other\n')
        self.git('commit', '-qm', 'extra doc')
        self.base = self.git('rev-parse', 'HEAD').decode().strip()
        self.write(SELECTOR.LINT_CONFIG, '{}\n')
        result = SELECTOR.select(self.root, self.base)
        self.assertTrue(result['configChanged'])
        self.assertEqual(result['files'], ['./README.md', './docs/Other.md'])

    def test_deleted_config_also_selects_all(self):
        self.write('.markdownlint.json', '{}')
        self.git('commit', '-qm', 'config')
        self.base = self.git('rev-parse', 'HEAD').decode().strip()
        self.git('rm', '-q', '.markdownlint.json')
        self.assertTrue(SELECTOR.select(self.root, self.base)['configChanged'])

    def test_invalid_or_nonancestor_base_refuses(self):
        with self.assertRaises(subprocess.CalledProcessError):
            SELECTOR.select(self.root, 'missing-reference')
        self.git('checkout', '-qb', 'other')
        self.write('other.txt', 'other')
        self.git('commit', '-qm', 'other branch')
        other = self.git('rev-parse', 'HEAD').decode().strip()
        self.git('checkout', '-q', '--detach', self.base)
        with self.assertRaises(ValueError):
            SELECTOR.select(self.root, other)

    def test_repo_subdirectory_is_not_promoted_to_root(self):
        (self.root / 'nested').mkdir()
        with self.assertRaises(ValueError):
            SELECTOR.select(self.root / 'nested', self.base)

    def test_glob_newline_and_traversing_paths_refuse(self):
        for name in ['../escape.md', '/absolute.md', 'line\nbreak.md', 'wild*.md', 'set[1].md', 'part?.md']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                SELECTOR.safe_path(name)
        self.write('wild*.md', '# Unsupported\n')
        with self.assertRaises(ValueError):
            SELECTOR.select(self.root, self.base)

    def test_changed_symlink_refuses(self):
        self.git('rm', '-q', 'README.md')
        (self.root / 'README.md').symlink_to(self.root / 'source.py')
        self.git('add', 'README.md')
        with self.assertRaises(ValueError):
            SELECTOR.select(self.root, self.base)

    def test_git_nul_newline_path_is_not_split_or_silently_ignored(self):
        self.write('docs/line\nbreak.md', '# Unsupported\n')
        with self.assertRaises(ValueError):
            SELECTOR.select(self.root, self.base)

    def test_redirected_parent_refuses(self):
        self.write('docs/Guide.md', '# Guide\n')
        (self.root / 'docs/Guide.md').unlink()
        (self.root / 'docs').rmdir()
        (self.root / 'outside').mkdir()
        (self.root / 'outside/Guide.md').write_text('# Redirected\n')
        (self.root / 'docs').symlink_to(self.root / 'outside', target_is_directory=True)
        with self.assertRaises(ValueError):
            SELECTOR.select(self.root, self.base)

    def test_output_preserves_spaces_and_uses_safe_multiline_block(self):
        path = self.root / 'output'
        SELECTOR.github_output(path, {'count': 1, 'files': ['./docs/Normal name.md']})
        lines = path.read_text().splitlines()
        self.assertEqual(lines[0], 'count=1')
        self.assertEqual(lines[2], './docs/Normal name.md')
        self.assertEqual(lines[1], 'files<<' + lines[3])

    def test_output_symlink_refuses_without_writing(self):
        target = self.root / 'target'
        target.write_text('preserve')
        link = self.root / 'output'
        link.symlink_to(target)
        with self.assertRaises(ValueError):
            SELECTOR.github_output(link, {'count': 0, 'files': []})
        self.assertEqual(target.read_text(), 'preserve')

    def test_output_payload_cannot_inject_lines_or_wrong_count(self):
        for result in ({'count': 1, 'files': ['./bad\ninjected.md']}, {'count': 2, 'files': ['./one.md']}):
            with self.subTest(result=result), self.assertRaises(ValueError):
                SELECTOR.github_output(self.root / 'output', result)
        self.assertFalse((self.root / 'output').exists())

    def test_empty_output_and_parent_traversal(self):
        output = self.root / 'output'
        SELECTOR.github_output(output, {'count': 0, 'files': []})
        self.assertTrue(output.read_text().startswith('count=0\nfiles<<'))
        with self.assertRaises(ValueError):
            SELECTOR.github_output(self.root / '../escape', {'count': 0, 'files': []})


if __name__ == '__main__':
    unittest.main()
