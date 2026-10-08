#!/usr/bin/env python3
"""Offline fail-closed source-bundle controls for the local package lane."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import tarfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from package_mingw_client import verify_bundle
import package_mingw_client as package_mingw


class CompiledClientIdentityTest(unittest.TestCase):
    def test_current_source_requires_branded_binary(self):
        with mock.patch.object(package_mingw, 'committed_file', return_value=b'OUTPUT_NAME "new-horizons"'):
            self.assertEqual(package_mingw.compiled_client_name(Path('.'), 'a' * 40), 'new-horizons.exe')
            with self.assertRaises(RuntimeError):
                package_mingw.compiled_client_name(Path('.'), 'a' * 40, 'a' * 40)

    def test_legacy_source_must_be_explicit_and_exact(self):
        with mock.patch.object(package_mingw, 'committed_file', return_value=b'OUTPUT_NAME "VCMI_client"'):
            for pin in (None, 'b' * 40):
                with self.assertRaises(RuntimeError):
                    package_mingw.compiled_client_name(Path('.'), 'a' * 40, pin)
            self.assertEqual(package_mingw.compiled_client_name(Path('.'), 'a' * 40, 'a' * 40), 'VCMI_client.exe')


class MinGWArtStagingTest(unittest.TestCase):
    """Real archive extraction and NHART validation, synthetic git transport only."""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.inputs = self.root / 'inputs'
        self.destination = self.root / 'package'
        self.inputs.mkdir()
        self.destination.mkdir()
        self.files = {name + '/fixture.json': b'{}' for name in package_mingw.RESOURCES}
        source = 'Mods/new-horizons/Images/unit.png'
        payload = b'synthetic art'
        self.files[source] = payload
        self.manifest = {'format': 1, 'requiredFamilies': ['fixture'],
                         'requiredFamilyCounts': {'fixture': 1}, 'entries': [{
            'source': source, 'resource': 'SPRITES/unit.png', 'size': len(payload),
            'sha256': hashlib.sha256(payload).hexdigest(), 'family': 'fixture',
            'origin': 'synthetic test', 'selection': 'explicit fixture', 'approval': 'test only'}]}
        for name, data in self.files.items():
            target = self.inputs / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        pack = self.inputs / package_mingw.common.RUNTIME_ART_PACK
        package_mingw.common.nhart.pack(self.manifest, self.inputs, pack)
        self.files[package_mingw.common.RUNTIME_ART_PACK] = pack.read_bytes()

    def git_transport(self, root, *args):
        if args[0] == 'show':
            self.assertTrue(args[1].startswith('synthetic-revision:'))
            source = args[1].split(':', 1)[1]
            if source == package_mingw.common.RUNTIME_ART_MANIFEST:
                return json.dumps(self.manifest).encode()
            self.assertIn(source, package_mingw.RUNTIME_NOTICES)
            return ('notice from pinned source: ' + source).encode()
        if args[0] == 'ls-tree':
            return package_mingw.common.RUNTIME_ART_MANIFEST.encode() if self.manifest else b''
        self.assertEqual(args[0], 'archive')
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode='w') as archive:
            for directory in package_mingw.RESOURCES:
                member = tarfile.TarInfo(directory)
                member.type = tarfile.DIRTYPE
                archive.addfile(member)
            for name, data in self.files.items():
                member = tarfile.TarInfo(name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        return output.getvalue()

    def stage(self, legacy=False):
        with mock.patch.object(package_mingw, 'git', side_effect=self.git_transport):
            package_mingw.stage_committed_resources(self.root, 'synthetic-revision', self.destination, legacy)

    def test_committed_pack_survives_without_duplicate_loose_art(self):
        self.stage()
        name = package_mingw.common.RUNTIME_ART_PACK
        self.assertEqual((self.destination / name).read_bytes(), self.files[name])
        self.assertFalse((self.destination / self.manifest['entries'][0]['source']).exists())
        package_mingw.common.verify_runtime_art(self.destination, self.manifest)

    def test_missing_pack_cannot_fall_back(self):
        del self.files[package_mingw.common.RUNTIME_ART_PACK]
        with self.assertRaisesRegex(RuntimeError, 'pack missing'):
            self.stage()

    def test_embedded_inventory_cannot_choose_its_own_expected_manifest(self):
        self.manifest['entries'][0]['approval'] = 'different selected revision'
        with self.assertRaisesRegex(RuntimeError, 'expected source manifest'):
            self.stage()

    def test_undeclared_loose_art_is_fatal(self):
        self.files['Mods/new-horizons/Content/sprites/extra.png'] = b'undeclared'
        with self.assertRaisesRegex(RuntimeError, 'Undeclared loose'):
            self.stage()

    def test_legacy_flag_cannot_bypass_new_contract(self):
        with self.assertRaisesRegex(RuntimeError, 'cannot bypass'):
            self.stage(legacy=True)

    def test_historical_loose_resources_require_explicit_lane(self):
        del self.files[package_mingw.common.RUNTIME_ART_PACK]
        self.manifest = None
        self.stage(legacy=True)
        self.assertEqual((self.destination / 'Mods/new-horizons/Images/unit.png').read_bytes(), b'synthetic art')
        self.assertFalse((self.destination / 'Mods/new-horizons/notices').exists())

    def test_same_cmake_notices_are_staged_from_pinned_source(self):
        self.stage()
        cmake = (Path(__file__).resolve().parents[2] / 'CMakeLists.txt').read_text()
        import re
        declaration = re.search(r'foreach\(NH_NOTICE_FAMILY IN ITEMS(.*?)\)', cmake, re.S).group(1)
        self.assertEqual(tuple(re.findall(r'"([^"]*)"', declaration)), package_mingw.NOTICE_FAMILIES)
        self.assertIn('docs/NHART_FORMAT.md docs/NHART_DELIVERY.md', cmake)
        self.assertIn('assets/new-horizons/sorcery-art/LICENSE', cmake)
        for source, target in package_mingw.RUNTIME_NOTICES.items():
            with self.subTest(source=source):
                self.assertEqual((self.destination / target).read_bytes(), ('notice from pinned source: ' + source).encode())

    def test_selected_config_can_differ_from_unshipped_authoring_copy(self):
        source = 'config/newHorizonsMagicAssets.json'
        selected = b'{"castingGlows":{},"guildBooks":{"selected":"complete"}}'
        self.manifest['entries'].append({
            'source': source, 'resource': 'CONFIG/newHorizonsMagicAssets.json', 'size': len(selected),
            'sha256': hashlib.sha256(selected).hexdigest(), 'family': 'fixture',
            'origin': 'synthetic test', 'selection': 'complete selected config', 'approval': 'test only'})
        self.manifest['requiredFamilyCounts']['fixture'] = 2
        (self.inputs / source).write_bytes(selected)
        package_mingw.common.nhart.pack(self.manifest, self.inputs, self.inputs / package_mingw.common.RUNTIME_ART_PACK)
        self.files[package_mingw.common.RUNTIME_ART_PACK] = (self.inputs / package_mingw.common.RUNTIME_ART_PACK).read_bytes()
        self.files[source] = b'{"castingGlows":{"legacy":"authoring only"}}'
        self.stage()
        self.assertFalse((self.destination / source).exists())
        self.assertEqual((self.destination / 'config/fixture.json').read_bytes(), b'{}')
        package_mingw.common.verify_runtime_art(self.destination, self.manifest)


class MinGWBundleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'source').write_bytes(b'original')
        self.manifest = {'files': {'source': hashlib.sha256(b'original').hexdigest()}}
        self.save()

    def save(self):
        (self.root / 'BUNDLE-IDENTITY.json').write_text(json.dumps(self.manifest))

    def test_exact_bundle(self):
        self.assertEqual(verify_bundle(self.root), self.manifest)

    def test_changed_file(self):
        (self.root / 'source').write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'Changed'):
            verify_bundle(self.root)

    def test_unlisted_file(self):
        (self.root / 'extra').write_text('not audited')
        with self.assertRaisesRegex(RuntimeError, 'inventory'):
            verify_bundle(self.root)

    def test_link_rejected(self):
        (self.root / 'alias').symlink_to('source')
        with self.assertRaisesRegex(RuntimeError, 'inventory/link'):
            verify_bundle(self.root)

    def test_empty_manifest_rejected(self):
        self.manifest['files'] = {}
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'inventory'):
            verify_bundle(self.root)


if __name__ == '__main__':
    unittest.main()
