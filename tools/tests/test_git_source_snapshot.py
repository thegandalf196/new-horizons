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
        self.run_git(root, 'config', 'core.symlinks', 'true')
        self.run_git(root, 'config', 'core.autocrlf', 'false')
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

    def test_install_required_nhart_docs_use_pinned_bytes_while_private_docs_stay_excluded(self):
        public_docs = {
            'docs/NHART_FORMAT.md': b'Committed public NHART format fixture\n',
            'docs/NHART_DELIVERY.md': b'Committed public NHART delivery fixture\n',
            'docs/NH_DOCUMENTATION_POLICY.md': b'Synthetic reviewed documentation policy\n',
            'docs/NH_ART_DIRECTION.md': b'Public art contract fixture\n',
            'docs/NH_INTEGRATION_GAPS.md': b'Public bounded integration issue fixture\n',
            'docs/NH_REGRESSION_GUARDS.md': b'Public regression contract fixture\n',
            'docs/NH_FUNCTIONAL_COMPLETION_MATRIX.md': b'Public bounded coverage index fixture\n',
        }
        private_files = (
            'docs/NH_PRIVATE_QUEUE_FIXTURE.md',
            'docs/NH_PRIVATE_COVERAGE_FIXTURE.md',
            'CI/deploy_rsa.enc',
        )
        for name, content in public_docs.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        for name in private_files:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'Excluded synthetic fixture\n')
        revision = self.commit(self.root)
        for name in public_docs:
            (self.root / name).write_bytes(b'Uncommitted future documentation\n')
        output = self.base / 'install-source.tar.gz'
        result = source_snapshot(self.root, revision, output)
        self.assertEqual(result['excluded'], sorted(private_files))
        self.assertEqual(result['source_entries'], len(public_docs))
        prefix = 'new-horizons-' + revision + '/'
        with tarfile.open(output) as archive:
            self.assertEqual(set(archive.getnames()), {prefix + name for name in public_docs})
            for name, content in public_docs.items():
                self.assertEqual(archive.extractfile(prefix + name).read(), content)
        for name in public_docs:
            self.assertEqual((self.root / name).read_bytes(), b'Uncommitted future documentation\n')

    def test_historical_revision_does_not_opt_in_from_mutable_or_untracked_policy(self):
        path = self.root / 'docs/NH_FUNCTIONAL_COMPLETION_MATRIX.md'
        path.parent.mkdir()
        path.write_text('Synthetic historical private content')
        revision = self.commit(self.root)
        policy = self.root / 'docs/NH_DOCUMENTATION_POLICY.md'
        policy.write_text('Untracked future policy')
        for label in ('untracked', 'committed-later'):
            if label == 'committed-later':
                self.commit(self.root)
            result = source_snapshot(self.root, revision, self.base / (label + '.tar.gz'))
            self.assertEqual(result['excluded'], ['docs/NH_FUNCTIONAL_COMPLETION_MATRIX.md'])
            self.assertEqual(result['source_entries'], 0)

    def test_symlink_policy_does_not_enable_document_export(self):
        (self.root / 'docs').mkdir()
        (self.root / 'docs/NH_INTEGRATION_GAPS.md').write_text('Synthetic historical private content')
        (self.root / 'docs/NH_DOCUMENTATION_POLICY.md').symlink_to('NH_INTEGRATION_GAPS.md')
        revision = self.commit(self.root)
        result = source_snapshot(self.root, revision, self.base / 'symlink-policy.tar.gz')
        self.assertEqual(result['excluded'], ['docs/NH_DOCUMENTATION_POLICY.md', 'docs/NH_INTEGRATION_GAPS.md'])
        self.assertEqual(result['source_entries'], 0)

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
