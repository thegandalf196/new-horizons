"""Optional fixed setup payload integrity; real system ELF bytes never executed."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import shutil
import os
import subprocess
import sys
from types import SimpleNamespace
import unittest

import test_linux_playable_snapshot as baseline
from linux_playable_snapshot import freeze, verify_snapshot, make_removable
from linux_snapshot_binaries import SETUP_NAME, SUPPORT, executable_abi


class LinuxSnapshotSetupTest(unittest.TestCase):
    setUp = baseline.LinuxPlayableSnapshotTest.setUp
    tearDown = baseline.LinuxPlayableSnapshotTest.tearDown
    freeze_candidate = baseline.LinuxPlayableSnapshotTest.freeze_candidate
    runtime_library = baseline.LinuxPlayableSnapshotTest.runtime_library

    def setup_candidate(self):
        shutil.copy2('/bin/true', self.client)
        self.helper = self.bin / SETUP_NAME
        shutil.copy2('/bin/true', self.helper)
        output = io.StringIO()
        with redirect_stdout(output):
            freeze(SimpleNamespace(client=self.client, resources=self.resources, store=self.store,
                                   promote=False, runtime_libraries=(), retain_resources_from=None,
                                   setup_client=self.helper))
        return Path(output.getvalue().strip())

    def test_setup_fixed_support_and_permissions_are_bound(self):
        candidate = self.setup_candidate()
        metadata = verify_snapshot(candidate)
        self.assertEqual(set(metadata['setup']), {SETUP_NAME, *SUPPORT})
        for name in metadata['setup']:
            self.assertEqual((candidate / name).stat().st_mode & 0o222, 0)
        self.assertIn('linux_assets_setup.py', metadata['files'])
        self.assertFalse((self.store / 'current.json').exists())

    def test_setup_mode_change_refused(self):
        candidate = self.setup_candidate()
        (candidate / SETUP_NAME).chmod(0o755)
        with self.assertRaisesRegex(RuntimeError, 'permissions'):
            verify_snapshot(candidate)

    def test_shared_library_cannot_masquerade_as_setup_program(self):
        provider = self.runtime_library()
        provider.chmod(0o755)
        with self.assertRaisesRegex(RuntimeError, 'program'):
            executable_abi(provider)

    def test_setup_payload_missing_support_is_rejected_even_with_updated_hashes(self):
        candidate = self.setup_candidate()
        make_removable(candidate)
        (candidate / 'linux_assets_setup.py').unlink()
        with self.assertRaises(RuntimeError):
            verify_snapshot(candidate)

    def test_legacy_snapshot_does_not_require_setup(self):
        candidate = self.freeze_candidate()
        self.assertNotIn('setup', verify_snapshot(candidate))
        self.assertFalse((candidate / SETUP_NAME).exists())

    def test_frozen_public_entry_preflights_without_source_checkout(self):
        candidate = self.setup_candidate()
        assets = self.root / 'original ü Complete'
        for name in ('Data', 'Maps', 'Mp3'):
            (assets / name).mkdir(parents=True)
        for name in ('h3bitmap.lod', 'h3sprite.lod', 'h3ab_bmp.lod', 'h3ab_spr.lod'):
            (assets / 'Data' / name).write_bytes(b'synthetic archive')
        profile = self.root / 'private profile'
        result = subprocess.run(['bash', str(candidate / 'Play-New-Horizons.sh'),
                                 '--assets', str(assets), '--profile', str(profile), '--verify-only'],
                                env={**os.environ, 'NH_PYTHON': sys.executable},
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertFalse(profile.exists())
        self.assertFalse(profile.with_name(profile.name + '.setup').exists())


if __name__ == '__main__':
    unittest.main()
