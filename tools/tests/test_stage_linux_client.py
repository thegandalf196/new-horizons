"""Offline integrity/privacy controls for staging, without executing the client."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from stage_linux_client import main, public_evidence, verify_candidate
from binary_privacy import inspect_bytes
from stage_runtime_fixture import StageFixture, QUARTET, elf_bytes, tree_bytes


class LinuxStageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        data = json.dumps({'source_commit': 'test'}).encode()
        (self.root / 'BUILD-IDENTITY.json').write_bytes(data)
        (self.root / 'SHA256SUMS').write_text(hashlib.sha256(data).hexdigest() + '  BUILD-IDENTITY.json\n')

    def test_staging_preserves_engine_identity_without_stale_feature_label(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = StageFixture(Path(temporary), self.root, providers=())
            data = (self.root / 'BUILD-IDENTITY.json').read_bytes()
            fixture.run(main)
            staged = verify_candidate(fixture.arguments['output'] / 'New-Horizons-Linux-x64')
            self.assertEqual(staged['source_commit'], fixture.identity['source_commit'])
            self.assertEqual(staged['packaging_commit'], 'packaging-revision')
            self.assertEqual(staged['scope'], 'Ubuntu26.04 system-dependent Linux preview; acceptance evidence is separate')
            self.assertEqual(len(staged['source_archives']), 3)
            self.assertEqual((self.root / 'BUILD-IDENTITY.json').read_bytes(), data)

    def test_exact_candidate(self):
        self.assertEqual(verify_candidate(self.root)['source_commit'], 'test')

    def test_extra_file_rejected(self):
        (self.root / 'extra').write_text('not in manifest')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_changed_bytes_rejected(self):
        (self.root / 'BUILD-IDENTITY.json').write_text('{}')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_traversal_rejected(self):
        (self.root / 'SHA256SUMS').write_text('0  ../outside\n')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_symlink_rejected(self):
        try:
            (self.root / 'link').symlink_to(self.root / 'BUILD-IDENTITY.json')
        except OSError:
            self.skipTest('Symlink permission unavailable')
        with self.assertRaises(RuntimeError):
            verify_candidate(self.root)

    def test_only_known_candidate_prefix_is_replaced(self):
        data = {str(self.root / 'vcmiclient'): {'NEEDED': str(self.root / 'libvcmi.so')}}
        self.assertEqual(public_evidence(data, self.root), {'bundled/vcmiclient': {'NEEDED': 'bundled/libvcmi.so'}})
        unknown = public_evidence('/home/private/build/library.so', self.root)
        self.assertTrue(inspect_bytes(unknown.encode()))


class RuntimeProviderStageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = StageFixture(Path(self.temporary.name))

    def reject(self, libraries=None):
        before = tree_bytes(self.fixture.candidate)
        with self.assertRaises(RuntimeError):
            self.fixture.run(main, libraries)
        self.assertFalse(self.fixture.arguments['output'].exists())
        self.assertEqual(tree_bytes(self.fixture.candidate), before)

    def test_complete_quartet_is_frozen_with_exact_origin_and_engine_bytes(self):
        fixture = self.fixture
        raw = tree_bytes(fixture.candidate)
        modes = {p.name: p.stat().st_mode for p in fixture.candidate.iterdir()}
        inputs = {p.name: p.read_bytes() for p in fixture.libraries}
        fixture.run(main)
        package = fixture.arguments['output'] / 'New-Horizons-Linux-x64'
        identity = verify_candidate(package)
        self.assertEqual(identity['binaries'], fixture.identity['binaries'])
        self.assertEqual(set(identity['packaged_elfs']), set(QUARTET) | {'new-horizons', 'libvcmi.so'})
        self.assertEqual(tree_bytes(fixture.candidate), raw)
        self.assertEqual({p.name: p.stat().st_mode for p in fixture.candidate.iterdir()}, modes)
        for path in fixture.libraries:
            self.assertEqual(path.read_bytes(), inputs[path.name])
            self.assertEqual((package / path.name).read_bytes(), inputs[path.name])
            self.assertFalse((package / path.name).is_symlink())
            self.assertFalse((package / path.name).stat().st_mode & 0o222)
            record = identity['bundled_runtime_libraries'][path.name]
            self.assertEqual(record['origin_label'], fixture.origins[path.name])
            self.assertEqual(record['bytes'], len(inputs[path.name]))

    def test_graph_relocation_preserves_origin_and_original_two_root_scope(self):
        fixture = self.fixture
        fixture.run(main)
        package = fixture.arguments['output'] / 'New-Horizons-Linux-x64'
        elf = json.loads((package / 'ELF-DEPENDENCIES.json').read_text())
        inventory = json.loads((package / 'DEPENDENCY-INVENTORY.json').read_text())
        provenance = json.loads((package / 'BUILD-PROVENANCE.json').read_text())
        roots = {'bundled/new-horizons', 'bundled/libvcmi.so'}
        self.assertEqual(set(elf['strong_symbol_check_roots']), roots)
        self.assertEqual(set(elf['shipped_strong_dynamic_imports_resolved']), roots)
        self.assertEqual(set(elf['unresolved_strong_imports']), roots)
        self.assertEqual(elf['scope'], fixture.elf['scope'])
        self.assertIn('no new symbol checks', elf['staging_relocation_scope'])
        self.assertEqual(elf['external_ELF_nodes'], 0)
        self.assertFalse(any(label.startswith('public-provider/') for label in elf['graph']))
        for name in QUARTET:
            self.assertIn('bundled/' + name, elf['graph'])
            self.assertEqual(elf['graph']['bundled/new-horizons']['NEEDED'][name], 'bundled/' + name)
        self.assertEqual(inventory['declared_public_provider_ELFs'], 0)
        self.assertEqual(inventory['declared_public_provider_origin_ELFs'], 4)
        self.assertEqual(inventory['declared_public_provider_files'], fixture.inventory['declared_public_provider_files'])
        self.assertEqual(inventory['headers'], fixture.inventory['headers'])
        self.assertEqual(provenance['source_inputs'], fixture.provenance['source_inputs'])
        self.assertEqual(fixture.elf['graph']['bundled/new-horizons']['NEEDED']['libSDL3.so.0'], fixture.origins['libSDL3.so.0'])

    def test_missing_all_providers_refuses_without_system_fallback(self):
        self.reject([])

    def test_missing_one_provider_refuses(self):
        self.reject(self.fixture.libraries[:-1])

    def test_extra_provider_refuses(self):
        extra = self.fixture.root / 'libExtra.so.0'
        extra.write_bytes(elf_bytes())
        self.reject([*self.fixture.libraries, extra])

    def test_duplicate_replacing_missing_provider_refuses(self):
        libs = self.fixture.libraries
        self.reject([libs[0], libs[0], *libs[2:]])

    def test_same_size_altered_provider_refuses_hash_mismatch(self):
        path = self.fixture.libraries[0]
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        self.reject()

    def test_size_mismatch_refuses(self):
        path = self.fixture.libraries[0]
        path.write_bytes(path.read_bytes() + b'altered')
        self.reject()

    def test_provider_symlink_refuses(self):
        path = self.fixture.libraries[0]
        target = self.fixture.root / 'real-provider'
        target.write_bytes(path.read_bytes())
        path.unlink()
        path.symlink_to(target)
        self.reject()

    def test_provider_parent_symlink_refuses(self):
        source = self.fixture.root / 'providers'
        source.mkdir()
        path = source / self.fixture.libraries[0].name
        path.write_bytes(self.fixture.libraries[0].read_bytes())
        link = self.fixture.root / 'provider-link'
        link.symlink_to(source, target_is_directory=True)
        self.reject([link / path.name, *self.fixture.libraries[1:]])

    def test_input_traversal_refuses(self):
        child = self.fixture.root / 'child'
        child.mkdir()
        path = child / '..' / self.fixture.libraries[0].name
        self.reject([path, *self.fixture.libraries[1:]])

    def test_unsafe_incoming_soname_refuses(self):
        needed = self.fixture.elf['graph']['bundled/new-horizons']['NEEDED']
        needed['../libEscape.so.0'] = needed.pop('libSDL3_ttf.so.0')
        self.reject()

    def test_collision_with_raw_payload_refuses(self):
        path = self.fixture.libraries[0]
        (self.fixture.candidate / path.name).write_bytes(path.read_bytes())
        self.fixture.write_candidate()
        self.reject()

    def test_elf_type_and_architecture_are_checked_after_matching_pins(self):
        path = self.fixture.libraries[0]
        original = path.read_bytes()
        for offset, value in ((4, 1), (5, 2), (16, 2), (18, 183)):
            with self.subTest(offset=offset):
                data = bytearray(original)
                data[offset] = value
                self.fixture.set_payload(path, bytes(data))
                self.reject()

    def test_provider_privacy_failure_refuses_before_stage_creation(self):
        path = self.fixture.libraries[0]
        self.fixture.set_payload(path, path.read_bytes() + b'/home/private/build/source')
        self.reject()

    def test_report_privacy_failure_refuses_before_stage_creation(self):
        self.fixture.provenance['extra'] = '/home/private/source/file'
        self.reject()

    def test_needed_provider_missing_from_graph_refuses(self):
        del self.fixture.elf['graph'][self.fixture.origins['libSDL3_ttf.so.0']]
        self.fixture.inventory['declared_public_provider_ELFs'] = 3
        self.reject()

    def test_graph_inventory_hash_mismatch_refuses(self):
        self.fixture.elf['graph'][self.fixture.origins['libSDL3_ttf.so.0']]['SHA256'] = '0' * 64
        self.reject()

    def test_inventory_count_mismatch_refuses(self):
        self.fixture.inventory['declared_public_provider_ELFs'] = 3
        self.reject()

    def test_ambiguous_incoming_soname_refuses(self):
        self.fixture.elf['graph']['bundled/new-horizons']['NEEDED']['libAlias.so.0'] = self.fixture.origins[QUARTET[0]]
        self.reject()

    def test_dangling_output_symlink_refuses_without_replacing_it(self):
        output = self.fixture.arguments['output']
        output.symlink_to(self.fixture.root / 'absent')
        raw = tree_bytes(self.fixture.candidate)
        with self.assertRaises(RuntimeError):
            self.fixture.run(main)
        self.assertTrue(output.is_symlink())
        self.assertEqual(tree_bytes(self.fixture.candidate), raw)

    def test_reports_cannot_claim_undeclared_root_strong_checks(self):
        self.fixture.elf['shipped_strong_dynamic_imports_resolved']['bundled/libSDL3.so.0'] = 1
        self.reject()

    def test_actual_provider_root_checks_relocate_without_expanding_scope(self):
        fixture = self.fixture
        fixture.elf['scope'] = 'Synthetic six-root aggregate pre-relocation evidence.'
        for index, label in enumerate(fixture.origins.values()):
            fixture.elf['shipped_strong_dynamic_imports_resolved'][label] = index + 2
            fixture.elf['unresolved_strong_imports'][label] = []
        fixture.run(main)
        package = fixture.arguments['output'] / 'New-Horizons-Linux-x64'
        elf = json.loads((package / 'ELF-DEPENDENCIES.json').read_text())
        expected = {'bundled/new-horizons': 1, 'bundled/libvcmi.so': 1}
        expected.update({'bundled/' + name: index + 2 for index, name in enumerate(QUARTET)})
        self.assertEqual(elf['shipped_strong_dynamic_imports_resolved'], expected)
        self.assertEqual(elf['unresolved_strong_imports'], dict.fromkeys(expected, []))
        self.assertEqual(elf['strong_symbol_check_roots'], sorted(expected))
        self.assertEqual(elf['scope'], fixture.elf['scope'])
        self.assertTrue(all(label in fixture.elf['graph'] for label in fixture.origins.values()))

    def test_provider_root_unresolved_or_mismatched_checks_refuse(self):
        label = self.fixture.origins[QUARTET[0]]
        self.fixture.elf['shipped_strong_dynamic_imports_resolved'][label] = 3
        self.reject()
        self.fixture.elf['unresolved_strong_imports'][label] = ['unresolved_symbol']
        self.reject()

    def test_relocated_reports_cannot_be_reused_to_omit_runtime_providers(self):
        from stage_runtime_libraries import admit_runtime_libraries, relocate_runtime_evidence
        fixture = self.fixture
        for provider_checks in (False, True):
            with self.subTest(provider_checks=provider_checks):
                if provider_checks:
                    for label in fixture.origins.values():
                        fixture.elf['shipped_strong_dynamic_imports_resolved'][label] = 2
                        fixture.elf['unresolved_strong_imports'][label] = []
                reports = {'ELF-DEPENDENCIES.json': fixture.elf,
                           'DEPENDENCY-INVENTORY.json': fixture.inventory,
                           'BUILD-PROVENANCE.json': fixture.provenance}
                libraries = admit_runtime_libraries(fixture.libraries, fixture.candidate,
                    fixture.identity, reports, fixture.inventory['dependency_source_archive_sha256'])
                relocated = relocate_runtime_evidence(reports, libraries)
                original = fixture.elf, fixture.inventory, fixture.provenance
                fixture.elf = relocated['ELF-DEPENDENCIES.json']
                fixture.inventory = relocated['DEPENDENCY-INVENTORY.json']
                fixture.provenance = relocated['BUILD-PROVENANCE.json']
                self.reject([])
                fixture.elf, fixture.inventory, fixture.provenance = original

    def test_source_companion_and_engine_identity_mismatches_refuse(self):
        for report, key, value in ((self.fixture.inventory, 'dependency_source_archive_sha256', '0' * 64),
                                   (self.fixture.provenance, 'source_archive_sha256', '0' * 64),
                                   (self.fixture.elf, 'source_commit', 'different')):
            with self.subTest(key=key):
                old = report[key]
                report[key] = value
                self.reject()
                report[key] = old

    def test_frozen_provider_mutation_is_detected_without_changing_raw(self):
        raw = tree_bytes(self.fixture.candidate)
        self.fixture.run(main)
        package = self.fixture.arguments['output'] / 'New-Horizons-Linux-x64'
        provider = package / QUARTET[0]
        provider.chmod(0o644)
        provider.write_bytes(b'changed')
        with self.assertRaises(RuntimeError):
            verify_candidate(package)
        self.assertEqual(tree_bytes(self.fixture.candidate), raw)


if __name__ == '__main__':
    unittest.main()
