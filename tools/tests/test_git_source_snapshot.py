#!/usr/bin/env python3
"""Source archives must use pinned objects, not mutable shared files."""
from pathlib import Path
from io import BytesIO
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from git_source_snapshot import source_snapshot
from source_export_policy import (
    RETIRED_PUBLIC_PATHS, forbidden_tracked_design, load_retired_public_paths, private_design_path,
)


class RetiredFinderMetadataTest(unittest.TestCase):
    def test_exact_index_entry_is_rejected_without_payload_or_worktree_reads(self):
        root = Path(__file__).resolve().parents[2]
        names = b'osx/dmg_DS_Store\0osx/DS_Store_Setup.scpt\0osx/dmg_background.png\0osx/dmg_background.psd\0osx/vcmi.icns\0osx/dmg_DS_Store.example\0'
        with patch('source_export_policy.subprocess.check_output', side_effect=[str(root) + '\n', names]) as metadata, \
                patch.object(Path, 'open', side_effect=AssertionError('metadata-only guard must not read a payload')):
            self.assertEqual(forbidden_tracked_design(root), ['osx/dmg_DS_Store'])
        self.assertEqual(metadata.call_count, 2)
        self.assertEqual(metadata.call_args.args[0][-2:], ['ls-files', '-z'])


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
        names = ['docs/NH_FUNCTIONAL_COMPLETION_MATRIX.md', 'docs/NH_UI_ASSET_INVENTORY.csv']
        (self.root / 'docs').mkdir()
        for name in names:
            (self.root / name).write_text('Synthetic historical private content')
        revision = self.commit(self.root)
        policy = self.root / 'docs/NH_DOCUMENTATION_POLICY.md'
        policy.write_text('Untracked future policy')
        for label in ('untracked', 'committed-later'):
            if label == 'committed-later':
                self.commit(self.root)
            result = source_snapshot(self.root, revision, self.base / (label + '.tar.gz'))
            self.assertEqual(result['excluded'], sorted(names))
            self.assertEqual(result['source_entries'], 0)

    def test_public_ui_inventory_survives_both_exports_without_private_lookalikes(self):
        attributes = Path(__file__).resolve().parents[2] / '.gitattributes'
        (self.root / '.gitattributes').write_bytes(attributes.read_bytes())
        (self.root / 'docs').mkdir()
        inventory = 'docs/NH_UI_ASSET_INVENTORY.csv'
        content = b'Item / ID,Art\nSynthetic public fixture,Provisional\n'
        (self.root / inventory).write_bytes(content)
        (self.root / 'docs/NH_DOCUMENTATION_POLICY.md').write_text('Synthetic reviewed policy\n')
        private = ['docs/NH_PRIVATE.md', 'docs/NH_PRIVATE.csv',
                   'docs/NH_UI_ASSET_INVENTORY_PRIVATE.csv',
                   'docs/NH_UI_ASSET_INVENTORY.csv.backup']
        for name in private:
            (self.root / name).write_text('Excluded synthetic fixture\n')
        revision = self.commit(self.root)
        (self.root / inventory).write_bytes(b'Uncommitted future inventory\n')
        (self.root / '.gitattributes').write_text('/docs/NH_* export-ignore\n')

        raw = subprocess.check_output(['git', '-C', str(self.root), 'archive', revision])
        with tarfile.open(fileobj=BytesIO(raw)) as archive:
            self.assertEqual(archive.extractfile(inventory).read(), content)
            self.assertTrue(set(private).isdisjoint(archive.getnames()))
        output = self.base / 'inventory-source.tar.gz'
        result = source_snapshot(self.root, revision, output)
        self.assertEqual(result['excluded'], sorted(private))
        prefix = 'new-horizons-' + revision + '/'
        with tarfile.open(output) as archive:
            self.assertEqual(archive.extractfile(prefix + inventory).read(), content)
            self.assertTrue({prefix + name for name in private}.isdisjoint(archive.getnames()))
        self.assertEqual((self.root / inventory).read_bytes(), b'Uncommitted future inventory\n')

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

    def test_private_design_is_excluded_without_losing_runtime_source(self):
        public = {'source.cpp': b'int value;\n', 'license.txt': b'Synthetic notice\n',
                  'docs/design-sources/README.md': b'Public provenance pointer\n'}
        private = {'docs/design-sources/New Horizons.md': b'Synthetic design\n',
                   'docs/design-sources/New Horizons.docx': b'Synthetic archive\n',
                   'docs/design-sources/future-draft.txt': b'Synthetic draft\n',
                   'tools/migrate-new-horizons-canonical-docx.py': b'# retired utility\n'}
        for name, content in (public | private).items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        revision = self.commit(self.root)
        output = self.base / 'private-design.tar.gz'
        result = source_snapshot(self.root, revision, output)
        self.assertEqual(result['excluded'], sorted(private))
        with tarfile.open(output) as archive:
            prefix = 'new-horizons-' + revision + '/'
            self.assertEqual(set(archive.getnames()), {prefix + name for name in public})
            for name, content in public.items():
                self.assertEqual(archive.extractfile(prefix + name).read(), content)

    def test_git_archive_attributes_handle_design_paths_with_spaces(self):
        attributes = Path(__file__).resolve().parents[2] / '.gitattributes'
        (self.root / '.gitattributes').write_bytes(attributes.read_bytes())
        folder = self.root / 'docs/design-sources'
        folder.mkdir(parents=True)
        for name in ('New Horizons.md', 'New Horizons.docx', 'README.md'):
            (folder / name).write_text('Synthetic fixture\n')
        revision = self.commit(self.root)
        raw = subprocess.check_output(['git', '-C', str(self.root), 'archive', revision])
        with tarfile.open(fileobj=BytesIO(raw)) as archive:
            self.assertIn('docs/design-sources/README.md', archive.getnames())
            self.assertNotIn('docs/design-sources/New Horizons.md', archive.getnames())
            self.assertNotIn('docs/design-sources/New Horizons.docx', archive.getnames())

    def test_publication_guard_rejects_force_added_design_and_retired_tool(self):
        expected = ['docs/design-sources/future-draft.md', 'tools/migrate-new-horizons-canonical-docx.py']
        for name in [*expected, 'docs/design-sources/README.md', 'source.cpp']:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('Synthetic fixture\n')
        self.commit(self.root)
        self.assertEqual(forbidden_tracked_design(self.root), expected)
        self.run_git(self.root, 'rm', '--', *expected)
        self.assertEqual(forbidden_tracked_design(self.root), [])
        with self.assertRaisesRegex(ValueError, 'actual public Git root'):
            forbidden_tracked_design(self.root / 'docs')

    def test_all_exact_retired_paths_are_excluded_without_neighboring_contracts(self):
        public = ['source.cpp', 'license.txt', 'docs/NH_AI_EXCHANGES.md',
                  'assets/new-horizons/academy/README.md',
                  'osx/DS_Store_Setup.scpt', 'osx/dmg_background.png',
                  'osx/dmg_background.psd', 'osx/vcmi.icns', 'osx/dmg_DS_Store.example',
                  'assets/new-horizons/academy/masters/adventure/avctowr0.prompt.txt.example',
                  'docs/NH_AGENT_START.md.example']
        self.assertEqual(len(RETIRED_PUBLIC_PATHS), 148)
        for name in [*RETIRED_PUBLIC_PATHS, *public]:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('Synthetic placeholder only\n')
        self.assertTrue(all(private_design_path(name) for name in RETIRED_PUBLIC_PATHS))
        self.assertTrue(all(not private_design_path(name) for name in public))
        historical = self.commit(self.root)
        (self.root / 'docs/NH_DOCUMENTATION_POLICY.md').write_text('Synthetic reviewed policy\n')
        current = self.commit(self.root)
        for label, revision in [('historical', historical), ('current', current)]:
            output = self.base / (label + '-retired.tar.gz')
            result = source_snapshot(self.root, revision, output)
            expected = set(RETIRED_PUBLIC_PATHS)
            if label == 'historical':
                expected.add('docs/NH_AI_EXCHANGES.md')
                expected.add('docs/NH_AGENT_START.md.example')
            else:
                expected.add('docs/NH_AGENT_START.md.example')
            self.assertEqual(result['excluded'], sorted(expected))
            with tarfile.open(output) as archive:
                prefix = 'new-horizons-' + revision + '/'
                actual = {name.removeprefix(prefix) for name in archive.getnames()}
                self.assertTrue(RETIRED_PUBLIC_PATHS.isdisjoint(actual))
                self.assertIn('license.txt', actual)
                self.assertIn('assets/new-horizons/academy/README.md', actual)
                self.assertIn(public[-2], actual)
                self.assertTrue({name for name in public if name.startswith('osx/')}.issubset(actual))

    def test_retired_force_added_index_entry_is_rejected_even_without_worktree_file(self):
        name = 'assets/new-horizons/academy/masters/adventure/avctowr0.prompt.txt'
        (self.root / '.gitignore').write_text('*.prompt.txt\n')
        path = self.root / name
        path.parent.mkdir(parents=True)
        path.write_text('Synthetic placeholder only\n')
        self.run_git(self.root, 'add', '-f', '--', name)
        path.unlink()
        self.assertEqual(forbidden_tracked_design(self.root), [name])
        self.run_git(self.root, 'rm', '--cached', '--', name)
        self.assertEqual(forbidden_tracked_design(self.root), [])

    def test_rename_into_exact_retired_name_is_rejected_not_its_neighbor(self):
        name = 'assets/new-horizons/academy/integration/town-layout.json'
        neighbor = name + '.example'
        path = self.root / neighbor
        path.parent.mkdir(parents=True)
        path.write_text('{}\n')
        self.commit(self.root)
        self.assertEqual(forbidden_tracked_design(self.root), [])
        self.run_git(self.root, 'mv', '--', neighbor, name)
        self.assertEqual(forbidden_tracked_design(self.root), [name])
        self.run_git(self.root, 'rm', '-f', '--', name)
        self.assertEqual(forbidden_tracked_design(self.root), [])

    def test_retired_policy_rejects_wrong_shapes_and_nonportable_paths(self):
        path = self.base / 'retired_public_paths.json'
        invalid = [{}, [], 'docs/example.md', {'paths': []}, [None], [1], [True],
                   [[]], [{}], [''], ['.'], ['..'], ['../example'], ['docs/../example'],
                   ['/docs/example'], ['docs//example'], ['docs/./example'], ['docs/'],
                   ['C:/docs/example'], ['docs\\example'], [' docs/example'],
                   ['docs/example\n'], ['docs/\0example'], ['docs/\x7fexample'],
                   ['docs/example', 'docs/example']]
        for value in invalid:
            with self.subTest(value=value):
                path.write_text(json.dumps(value), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'Invalid retired-path policy retired_public_paths.json:'):
                    load_retired_public_paths(path)
        path.write_text('["docs/example.md", "assets/example.json"]', encoding='utf-8')
        self.assertEqual(load_retired_public_paths(path), {'docs/example.md', 'assets/example.json'})

    def test_retired_policy_missing_malformed_and_non_utf8_refuse(self):
        path = self.base / 'retired_public_paths.json'
        for content in (None, b'[broken', b'["\xff"]'):
            with self.subTest(content=content):
                if content is not None:
                    path.write_bytes(content)
                with self.assertRaisesRegex(ValueError, 'retired_public_paths.json: cannot read UTF-8 JSON'):
                    load_retired_public_paths(path)


if __name__ == '__main__':
    unittest.main()
