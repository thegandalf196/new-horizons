#!/usr/bin/env python3
"""Source archives must use pinned objects, not mutable shared files."""
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from git_source_snapshot import source_snapshot


class GitSourceSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / 'repository'
        self.root.mkdir()
        self.initialize(self.root)

    def run_git(self, root, *args):
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.DEVNULL).decode().strip()

    def initialize(self, root):
        self.run_git(root, 'init', '-q')
        self.run_git(root, 'config', 'user.name', 'thegandalf196')
        self.run_git(root, 'config', 'user.email', 'thegandalf196@users.noreply.github.com')

    def commit(self, root):
        self.run_git(root, 'add', '.')
        self.run_git(root, 'commit', '-qm', 'Synthetic source fixture')
        return self.run_git(root, 'rev-parse', 'HEAD')

    def test_pinned_bytes_exclusions_and_reproducibility(self):
        (self.root / 'source.cpp').write_text('committed code')
        (self.root / 'docs').mkdir()
        (self.root / 'docs/NH_PRIVATE.md').write_text('excluded diary fixture')
        revision = self.commit(self.root)
        (self.root / 'source.cpp').write_text('uncommitted future changes')
        (self.root / 'untracked.txt').write_text('not corresponding source')
        first, second = self.base / 'one.tar.gz', self.base / 'two.tar.gz'
        result = source_snapshot(self.root, revision, first)
        source_snapshot(self.root, revision, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(result['excluded'], ['docs/NH_PRIVATE.md'])
        with tarfile.open(first) as archive:
            self.assertEqual(archive.getnames(), ['new-horizons-' + revision + '/source.cpp'])
            self.assertEqual(archive.extractfile(archive.getmembers()[0]).read(), b'committed code')
        self.assertEqual((self.root / 'source.cpp').read_text(), 'uncommitted future changes')

    def test_submodule_uses_parent_pinned_commit(self):
        child = self.root / 'dependency'
        child.mkdir()
        self.initialize(child)
        (child / 'dep.cpp').write_text('pinned dependency')
        child_revision = self.commit(child)
        self.run_git(self.root, 'update-index', '--add', '--cacheinfo', '160000,' + child_revision + ',dependency')
        self.run_git(self.root, 'commit', '-qm', 'Pin synthetic dependency')
        revision = self.run_git(self.root, 'rev-parse', 'HEAD')
        (child / 'dep.cpp').write_text('later dependency')
        self.commit(child)
        output = self.base / 'submodule.tar.gz'
        source_snapshot(self.root, revision, output)
        with tarfile.open(output) as archive:
            self.assertEqual(archive.extractfile('new-horizons-' + revision + '/dependency/dep.cpp').read(), b'pinned dependency')
        self.assertEqual((child / 'dep.cpp').read_text(), 'later dependency')

    def test_uninitialized_submodule_requires_pinned_bare_cache(self):
        dependency = self.base / 'external'
        dependency.mkdir()
        self.initialize(dependency)
        (dependency / 'dep.cpp').write_text('cached exact source')
        pinned = self.commit(dependency)
        self.run_git(self.root, 'update-index', '--add', '--cacheinfo', '160000,' + pinned + ',dependency')
        self.run_git(self.root, 'commit', '-qm', 'Pin unavailable worktree')
        revision = self.run_git(self.root, 'rev-parse', 'HEAD')
        output = self.base / 'cached.tar.gz'
        with self.assertRaisesRegex(RuntimeError, 'Uninitialized submodule'):
            source_snapshot(self.root, revision, output)
        cache = self.base / 'cache'
        cache.mkdir()
        self.run_git(self.root, 'clone', '--bare', str(dependency), str(cache / (pinned + '.git')))
        source_snapshot(self.root, revision, output, cache)
        with tarfile.open(output) as archive:
            self.assertEqual(archive.extractfile('new-horizons-' + revision + '/dependency/dep.cpp').read(), b'cached exact source')
        self.assertFalse((self.root / 'dependency').exists())

    def test_escaping_link_rejected(self):
        (self.root / 'escape').symlink_to('../../outside')
        revision = self.commit(self.root)
        output = self.base / 'unsafe.tar.gz'
        with self.assertRaisesRegex(RuntimeError, 'Escaping archive link'):
            source_snapshot(self.root, revision, output)
        self.assertFalse(output.exists())

    def test_existing_archive_preserved(self):
        (self.root / 'source.cpp').write_text('fixture')
        revision = self.commit(self.root)
        output = self.base / 'existing.tar.gz'
        output.write_bytes(b'previous evidence')
        with self.assertRaisesRegex(RuntimeError, 'already exists'):
            source_snapshot(self.root, revision, output)
        self.assertEqual(output.read_bytes(), b'previous evidence')


if __name__ == '__main__':
    unittest.main()
