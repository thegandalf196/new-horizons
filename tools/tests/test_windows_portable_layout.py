"""Process-free packaging risks: two PE domains, clean root, no hidden fallback."""
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import windows_portable_layout as layout
import package_new_horizons_windows as package


class PortableLayoutTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / 'package'
        self.stage.mkdir()
        self.install = self.root / 'install'
        (self.install / 'bootstrap').mkdir(parents=True)
        (self.install / 'bootstrap/new-horizons.exe').write_bytes(b'BOOTSTRAP')
        for name in ('new-horizons.exe', 'VCMI_lib.dll', 'Start-New-Horizons.ps1', 'Launch-Process.ps1', 'Setup-Progress.ps1'):
            (self.stage / name).write_bytes(name.encode())

    @staticmethod
    def dumpbin(*args):
        return {'/headers': '8664 machine (x64)', '/dependents': '    KERNEL32.dll\n    USER32.dll',
                '/exports': ''}[args[2]]

    def assemble(self):
        with patch.object(layout.shutil, 'which', return_value='dumpbin'):
            return layout.assemble_portable_root(self.stage, self.install, self.dumpbin,
                                                 package.SYSTEM_DLLS, package.write_json)

    def test_root_is_clean_and_identical_named_engine_is_preserved(self):
        runtime = self.assemble()
        self.assertEqual({p.name for p in self.stage.iterdir()}, {'runtime', 'new-horizons.exe'})
        self.assertEqual((self.stage / 'new-horizons.exe').read_bytes(), b'BOOTSTRAP')
        self.assertEqual((runtime / 'new-horizons.exe').read_bytes(), b'new-horizons.exe')
        report = json.loads((runtime / 'BOOTSTRAP-IMPORTS.json').read_text())
        self.assertEqual(set(report), {'KERNEL32.dll', 'USER32.dll'})

    def test_installed_setup_is_audited_independently_before_staging(self):
        setup = self.install / 'bootstrap/Setup.exe'
        setup.write_bytes(b'MANUAL ENTRY')
        self.assemble()
        self.assertEqual((self.stage / 'Setup.exe').read_bytes(), b'MANUAL ENTRY')
        layout.assert_portable_layout(self.stage, require_setup=True)
        self.assertTrue((self.stage / 'runtime/MANUAL-SETUP-IMPORTS.json').is_file())

    def test_setup_app_dependency_cannot_borrow_game_dlls(self):
        (self.install / 'bootstrap/Setup.exe').write_bytes(b'MANUAL ENTRY')
        def dumpbin(*args):
            if args[2] == '/dependents' and args[3].endswith('Setup.exe'):
                return '    SDL3.dll'
            return self.dumpbin(*args)
        with patch.object(layout.shutil, 'which', return_value='dumpbin'):
            with self.assertRaisesRegex(RuntimeError, 'non-system'):
                layout.assemble_portable_root(self.stage, self.install, dumpbin,
                                             package.SYSTEM_DLLS, package.write_json)
        self.assertFalse((self.stage / 'runtime').exists())
        self.assertEqual((self.stage / 'new-horizons.exe').read_bytes(), b'new-horizons.exe')

    def test_manual_setup_is_the_only_permitted_root_sidecar(self):
        self.assemble()
        with self.assertRaises(RuntimeError):
            layout.assert_portable_layout(self.stage, require_setup=True)
        (self.stage / 'Setup.exe').write_bytes(b'INDEPENDENT SYSTEM-ONLY PE')
        layout.assert_portable_layout(self.stage, require_setup=True)
        (self.stage / 'arbitrary.exe').write_bytes(b'UNAPPROVED')
        with self.assertRaises(RuntimeError):
            layout.assert_portable_layout(self.stage, require_setup=True)

    def test_manual_setup_cannot_be_a_directory_or_link(self):
        self.assemble()
        setup = self.stage / 'Setup.exe'
        setup.mkdir()
        with self.assertRaisesRegex(RuntimeError, 'regular file'):
            layout.assert_portable_layout(self.stage)
        setup.rmdir()
        try:
            setup.symlink_to(self.stage / 'new-horizons.exe')
        except OSError:
            self.skipTest('Host does not support ordinary user symlinks')
        with self.assertRaisesRegex(RuntimeError, 'regular file'):
            layout.assert_portable_layout(self.stage)

    def test_root_cannot_import_even_a_bundled_engine_or_crt_dll(self):
        for dependency in ('VCMI_lib.dll', 'SDL3.dll', 'VCRUNTIME140.dll', 'MSVCP140.dll'):
            def dumpbin(*args):
                return self.dumpbin(*args) if args[2] != '/dependents' else '    ' + dependency
            with self.subTest(dependency=dependency), self.assertRaisesRegex(RuntimeError, 'non-system'):
                layout.audit_bootstrap(self.install / 'bootstrap/new-horizons.exe', dumpbin,
                                       package.SYSTEM_DLLS, 'dumpbin')

    def test_export_forwarder_and_architecture_cannot_bypass_gate(self):
        for option, text in (('/exports', '(forwarded to VCRUNTIME140.foo)'),
                             ('/headers', '14C machine (x86)')):
            def dumpbin(*args):
                return text if args[2] == option else self.dumpbin(*args)
            with self.subTest(option=option), self.assertRaises(RuntimeError):
                layout.audit_bootstrap(Path('stub.exe'), dumpbin, package.SYSTEM_DLLS, 'dumpbin')

    def test_missing_bootstrap_does_not_relabel_game_as_entry_point(self):
        (self.install / 'bootstrap/new-horizons.exe').unlink()
        with self.assertRaisesRegex(RuntimeError, 'bootstrap missing'):
            self.assemble()
        self.assertEqual((self.stage / 'new-horizons.exe').read_bytes(), b'new-horizons.exe')

    def test_linked_payload_is_rejected(self):
        try:
            (self.stage / 'alias.dll').symlink_to(self.stage / 'VCMI_lib.dll')
        except OSError:
            self.skipTest('This host does not permit ordinary user symlinks')
        with self.assertRaisesRegex(RuntimeError, 'Linked'):
            self.assemble()

    def test_extra_executable_is_rejected(self):
        self.assemble()
        (self.stage / 'runtime/smoke.exe').write_bytes(b'NOT SHIPPING')
        with self.assertRaisesRegex(RuntimeError, 'Unexpected executable'):
            layout.assert_portable_layout(self.stage)

    def test_nested_dll_cannot_escape_the_flat_runtime_audit(self):
        self.assemble()
        (self.stage / 'runtime/config').mkdir()
        (self.stage / 'runtime/config/hidden.DLL').write_bytes(b'NOT AUDITED')
        with self.assertRaisesRegex(RuntimeError, 'audited flat'):
            layout.assert_portable_layout(self.stage)

    def test_root_metadata_is_rejected(self):
        self.assemble()
        (self.stage / 'README.txt').write_text('belongs inside runtime')
        with self.assertRaisesRegex(RuntimeError, 'only'):
            layout.assert_portable_layout(self.stage)


if __name__ == '__main__':
    unittest.main()
