"""Synthetic installed-source/real NHART tests; no game or Git execution."""
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import assemble_linux_candidate as assembler
from stage_linux_client import verify_candidate
from verify_new_horizons_art_install import nhart, RUNTIME_ART_PACK


class AssembleLinuxCandidateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.addCleanup(self.make_writable)
        self.install = self.root / 'normal install'
        self.resources = self.install / 'share/vcmi'
        self.source = self.root / 'engine-source.tar.gz'
        self.repository = self.root / 'source-repository'
        self.repository.mkdir()
        self.manifest = self.root / 'runtime-manifest.json'
        self.output = self.root / 'raw-candidate'
        self.commit = 'a' * 40
        self.source_data = {'tools/new-horizons-launch.sh': b'#!/bin/sh\nexit 0\n',
                            'license.txt': b'Synthetic license fixture',
                            'AUTHORS.h': b'Synthetic attribution fixture'}
        for tree in assembler.TREES:
            (self.resources / tree).mkdir(parents=True)
        for name in assembler.REQUIRED_FILES:
            (self.resources / name).parent.mkdir(parents=True, exist_ok=True)
            (self.resources / name).write_bytes(b'{}')
        payload = b'Synthetic bitmap payload, no original artwork'
        inputs = self.root / 'pack-input'
        source_name = 'Mods/new-horizons/Images/test.png'
        (inputs / source_name).parent.mkdir(parents=True)
        (inputs / source_name).write_bytes(payload)
        expected = {'format': 1, 'requiredFamilies': ['fixture'],
                    'requiredFamilyCounts': {'fixture': 1}, 'entries': [{
                        'source': source_name, 'resource': 'SPRITES/test.png',
                        'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
                        'family': 'fixture', 'origin': 'synthetic', 'selection': 'test',
                        'approval': 'test only'}]}
        self.manifest.write_text(json.dumps(expected))
        nhart.pack(expected, inputs, self.resources / RUNTIME_ART_PACK)
        (self.resources / 'config/filesystem.json').write_text(json.dumps({'filesystem': {
            '': [{'type': 'nhart', 'path': RUNTIME_ART_PACK, 'overlay': True}]}}))
        (self.resources / 'Mods/new-horizons/mod.json').write_text(json.dumps({'filesystem': {
            '': [{'type': 'nhart', 'path': '/NewHorizons.nhart'}]}}))
        self.notice_data = {}
        for name, origin in assembler.NOTICES.items():
            data = ('Synthetic notice ' + origin).encode()
            path = self.resources / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.notice_data[origin] = data
            self.source_data[origin] = data
        platform_outputs = {}
        for name, origin in assembler.PLATFORM_FILES.items():
            data = ('Synthetic platform file ' + origin).encode()
            path = self.install / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.source_data[origin] = data
            if origin.endswith('.png'):
                platform_outputs[origin] = hashlib.sha256(data).hexdigest()
        provenance = json.dumps({'outputs': platform_outputs}).encode()
        origin = assembler.NOTICES[assembler.PLATFORM_PROVENANCE]
        self.source_data[origin] = self.notice_data[origin] = provenance
        (self.resources / assembler.PLATFORM_PROVENANCE).write_bytes(provenance)
        # Only ELF identity bytes are synthetic; no loader/decode acceptance claimed.
        header = b'\x7fELF\x02\x01' + b'\x00' * 12 + b'\x3e\x00'
        for name in ('bin/new-horizons', 'lib/libvcmi.so'):
            path = self.install / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(header + self.commit.encode())
        (self.install / 'bin/new-horizons').chmod(0o755)
        self.refresh_source()
        self.before = self.inventory(self.install)

    def make_writable(self):
        for path in self.root.rglob('*'):
            if path.is_dir():
                path.chmod(0o755)
        self.root.chmod(0o755)

    @staticmethod
    def inventory(root):
        return {p.relative_to(root).as_posix(): assembler.digest(p)
                for p in root.rglob('*') if p.is_file()}

    def refresh_source(self):
        for path in self.resources.rglob('*'):
            if path.is_file() and not path.is_symlink():
                name = path.relative_to(self.resources).as_posix()
                if name not in assembler.NOTICES:
                    self.source_data[name] = path.read_bytes()
        self.source_data['assets/new-horizons/runtime-art-manifest.json'] = self.manifest.read_bytes()
        with tarfile.open(self.source, 'w:gz') as archive:
            for name, data in self.source_data.items():
                member = tarfile.TarInfo('new-horizons-' + self.commit + '/' + name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))

    def assemble(self):
        with patch.object(assembler, 'source_notice', side_effect=lambda repo, commit, name: self.notice_data[name]):
            return assembler.assemble(self.install, self.source, self.commit,
                                      self.repository, self.manifest, self.output)

    def rejected(self, message):
        with self.assertRaisesRegex(RuntimeError, message):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_complete_candidate_is_stage_contract_compatible_and_inputs_unchanged(self):
        result = self.assemble()
        self.assertEqual(verify_candidate(self.output), result)
        self.assertEqual(result['source_commit'], self.commit)
        self.assertEqual(result['source_archive_sha256'], assembler.digest(self.source))
        self.assertEqual(result['art_verification']['mountsVerified'], 2)
        self.assertEqual(self.before, self.inventory(self.install))
        self.assertFalse(any(p.is_symlink() for p in self.output.rglob('*')))
        self.assertFalse(self.output.stat().st_mode & 0o222)
        self.assertTrue((self.output / 'new-horizons').stat().st_mode & 0o111)
        self.assertIn('separate BUILD-PROVENANCE', result['scope'])

    def test_noncurated_installed_assets_profiles_and_tools_are_not_copied(self):
        for name in ('share/vcmi/Maps/private.h3m', 'share/vcmi/Data/H3bitmap.lod',
                     'profile/settings.json', 'bin/vcmibuilder', 'share/vcmi/Mods/demo/mod.json',
                     'share/icons/hicolor/256x256/apps/vcmiclient.png',
                     'share/applications/unrelated.desktop', 'share/icons/private-master.png'):
            path = self.install / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'Never shipped')
        self.assemble()
        self.assertFalse(any(p.name in ('private.h3m', 'H3bitmap.lod', 'settings.json', 'vcmibuilder')
                             for p in self.output.rglob('*')))
        self.assertFalse((self.output / 'Mods/demo').exists())
        self.assertFalse((self.output / 'share/icons/hicolor/256x256/apps/vcmiclient.png').exists())
        self.assertFalse((self.output / 'share/applications/unrelated.desktop').exists())
        self.assertFalse((self.output / 'share/icons/private-master.png').exists())

    def test_exact_platform_files_and_notice_are_source_bound_and_stage_checked(self):
        result = self.assemble()
        self.assertEqual(len(assembler.PLATFORM_FILES), 10)
        self.assertEqual(set(result['platform_icon_files']), set(assembler.PLATFORM_FILES))
        for name, origin in assembler.PLATFORM_FILES.items():
            self.assertEqual((self.output / name).read_bytes(), self.source_data[origin])
            self.assertEqual(result['platform_icon_files'][name], assembler.digest(self.output / name))
        notice = self.output / assembler.PLATFORM_PROVENANCE
        self.assertEqual(notice.read_bytes(), self.source_data[assembler.NOTICES[assembler.PLATFORM_PROVENANCE]])
        self.assertEqual(result['platform_icon_provenance_sha256'], assembler.digest(notice))
        self.assertEqual(verify_candidate(self.output), result)
        self.assertEqual(self.before, self.inventory(self.install))

    def test_missing_platform_icon_or_desktop_refused_before_output(self):
        for name in (next(iter(assembler.PLATFORM_FILES)), 'share/applications/new-horizons.desktop'):
            with self.subTest(path=name):
                path = self.install / name
                original = path.read_bytes()
                path.unlink()
                self.rejected('Required regular input missing')
                path.write_bytes(original)

    def test_platform_source_and_provenance_hash_mismatch_refused(self):
        name, origin = next(iter(assembler.PLATFORM_FILES.items()))
        path = self.install / name
        original = path.read_bytes()
        path.write_bytes(b'Changed installed icon')
        self.rejected('Installed bytes differ from source')
        path.write_bytes(original)
        notice_origin = assembler.NOTICES[assembler.PLATFORM_PROVENANCE]
        record = json.loads(self.source_data[notice_origin])
        record['outputs'][origin] = '0' * 64
        altered = json.dumps(record).encode()
        self.source_data[notice_origin] = altered
        (self.resources / assembler.PLATFORM_PROVENANCE).write_bytes(altered)
        self.refresh_source()
        self.rejected('Platform icon hash differs from provenance')

    def test_missing_source_platform_file_refused(self):
        origin = next(iter(assembler.PLATFORM_FILES.values()))
        del self.source_data[origin]
        self.refresh_source()
        self.rejected('Missing regular source archive member')

    def test_platform_files_remain_in_manifest_against_postassembly_tampering(self):
        self.assemble()
        path = self.output / next(iter(assembler.PLATFORM_FILES))
        path.chmod(0o644)
        path.write_bytes(b'Altered after source admission')
        with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
            verify_candidate(self.output)

    def test_shared_notice_install_route_matches_both_packaging_platforms(self):
        repository = Path(__file__).resolve().parents[2]
        text = (repository / 'CMakeLists.txt').read_text()
        self.assertIn('install(FILES assets/new-horizons/platform-icon-provenance.json\n'
                      '\t\t\tDESTINATION ${DATA_DIR}/Mods/new-horizons/notices/provenance)', text)
        self.assertEqual(assembler.NOTICES[assembler.PLATFORM_PROVENANCE],
                         'assets/new-horizons/platform-icon-provenance.json')

    def test_windows_curated_staging_retains_same_platform_notice_without_loose_master(self):
        from package_new_horizons_windows import stage_engine_resources
        package = self.root / 'windows-resource-stage'
        package.mkdir()
        stage_engine_resources(self.resources, package, json.loads(self.manifest.read_text()))
        self.assertEqual((package / assembler.PLATFORM_PROVENANCE).read_bytes(),
                         self.source_data[assembler.NOTICES[assembler.PLATFORM_PROVENANCE]])
        self.assertFalse((package / 'share/icons').exists())
        self.assertFalse((package / 'clientapp/icons').exists())

    def test_existing_output_refused_without_change(self):
        self.output.mkdir()
        sentinel = self.output / 'sentinel'
        sentinel.write_bytes(b'unchanged')
        with self.assertRaisesRegex(RuntimeError, 'existing output'):
            self.assemble()
        self.assertEqual(sentinel.read_bytes(), b'unchanged')

    def test_unsafe_overlap_refused(self):
        self.output = self.install / 'nested'
        self.rejected('overlaps')

    def test_symlink_refused(self):
        (self.resources / 'config/linked').symlink_to(self.manifest)
        self.rejected('symlinks')

    def test_missing_client_refused(self):
        (self.install / 'bin/new-horizons').unlink()
        self.rejected('regular input missing')

    def test_malformed_source_commit_refused(self):
        self.commit = 'HEAD'
        self.rejected('exact full lowercase SHA')

    def test_non_elf_binary_refused(self):
        (self.install / 'bin/new-horizons').write_bytes(b'Not an ELF executable')
        self.rejected('Expected Linux ELF64')

    def test_archived_notices_need_no_git_fallback(self):
        with patch.object(assembler, 'source_notice', side_effect=AssertionError('No Git needed')) as notices:
            assembler.assemble(self.install, self.source, self.commit,
                               self.repository, self.manifest, self.output)
        notices.assert_not_called()

    def test_legacy_absent_notices_use_exact_revision(self):
        for name in ('docs/NHART_FORMAT.md', 'docs/NHART_DELIVERY.md'):
            del self.source_data[name]
        self.refresh_source()
        with patch.object(assembler, 'source_notice', side_effect=lambda repo, commit, name: self.notice_data[name]) as notices:
            assembler.assemble(self.install, self.source, self.commit,
                               self.repository, self.manifest, self.output)
        self.assertEqual(notices.call_count, 2)
        self.assertEqual({call.args for call in notices.call_args_list}, {
            (self.repository, self.commit, 'docs/NHART_FORMAT.md'),
            (self.repository, self.commit, 'docs/NHART_DELIVERY.md')})

    def test_ambiguous_client_refused(self):
        shutil.copyfile(self.install / 'bin/new-horizons', self.install / 'bin/vcmiclient')
        self.rejected('Ambiguous')

    def test_mismatched_binary_revision_refused(self):
        path = self.install / 'lib/libvcmi.so'
        path.write_bytes(path.read_bytes().replace(self.commit.encode(), b'b' * 40))
        self.rejected('source revision')

    def test_wrong_archive_commit_refused(self):
        self.commit = 'b' * 40
        path = self.install / 'lib/libvcmi.so'
        path.write_bytes(path.read_bytes().replace(b'a' * 40, self.commit.encode()))
        self.rejected('source archive root')

    def test_source_resource_mismatch_refused(self):
        (self.resources / 'config/newHorizonsCombat.json').write_bytes(b'{"changed":true}')
        self.rejected('Installed bytes differ')

    def test_extra_or_missing_curated_file_refused(self):
        (self.resources / 'config/private.json').write_bytes(b'{}')
        self.rejected('inventory differs')
        (self.resources / 'config/private.json').unlink()
        (self.resources / 'scripts/damage/damageCalculator.lua').unlink()
        self.rejected('Missing required')

    def test_missing_or_changed_notice_refused(self):
        name = next(iter(assembler.NOTICES))
        (self.resources / name).write_bytes(b'changed notice')
        self.rejected('Installed bytes differ')
        (self.resources / name).unlink()
        self.rejected('Missing required')

    def test_manifest_different_from_source_refused(self):
        self.source_data['assets/new-horizons/runtime-art-manifest.json'] = b'{}'
        # Preserve the genuine installed pack/manifest, alter only source evidence.
        with tarfile.open(self.source, 'w:gz') as archive:
            for name, data in self.source_data.items():
                member = tarfile.TarInfo('new-horizons-' + self.commit + '/' + name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        self.rejected('manifest differs')

    def test_missing_pack_and_loose_fallback_refused(self):
        pack = self.resources / RUNTIME_ART_PACK
        original = pack.read_bytes()
        pack.unlink()
        self.rejected('pack missing')
        pack.write_bytes(original)
        fallback = self.resources / 'Mods/new-horizons/Images/extra.png'
        fallback.parent.mkdir()
        fallback.write_bytes(b'Unselected loose input')
        self.rejected('loose runtime art fallback')

    def test_binary_privacy_refused_before_output(self):
        with (self.install / 'lib/libvcmi.so').open('ab') as stream:
            stream.write(b'/home/personal/private/path\x00')
        self.rejected('Personal home/profile')


if __name__ == '__main__':
    unittest.main()
