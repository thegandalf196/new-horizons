"""Optional native setup root must carry independent fixed-payload evidence."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from stage_linux_client import main, verify_candidate
from linux_snapshot_binaries import SETUP_NAME, SUPPORT
from stage_runtime_fixture import StageFixture

ROOT = Path(__file__).resolve().parents[2]


class StageLinuxSetupTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.fixture = StageFixture(self.root, providers=())
        for name in ('new-horizons', SETUP_NAME):
            shutil.copy2('/bin/true', self.fixture.candidate / name)
            digest = hashlib.sha256((self.fixture.candidate / name).read_bytes()).hexdigest()
            self.fixture.identity['binaries'][name] = digest
            self.fixture.elf['graph']['bundled/' + name] = {'SHA256': digest, 'NEEDED': {}, 'RUNPATH_or_RPATH': ['$ORIGIN']}
            self.fixture.elf['shipped_strong_dynamic_imports_resolved']['bundled/' + name] = 1
            self.fixture.elf['unresolved_strong_imports']['bundled/' + name] = []
        for name, source in SUPPORT.items():
            shutil.copy2(ROOT / source, self.fixture.candidate / name)
            (self.fixture.candidate / name).chmod(0o555 if name.endswith('.sh') else 0o444)
        for relative in ('config/filesystem.json', 'Mods/vcmi/mod.json'):
            path = self.fixture.candidate / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{}')
        self.write_candidate()

    def write_candidate(self):
        self.fixture.write_candidate()
        root = self.fixture.candidate
        (root / 'SHA256SUMS').write_text(''.join(
            hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(root).as_posix() + '\n'
            for p in sorted(root.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))

    def test_audited_fixed_helper_and_readonly_support_stage_successfully(self):
        # The fixture's git provider is synthetic; return actual maintained support
        # bytes to exercise the final package's real preflight below.
        def content(argv, **kwargs):
            relative = argv[-1].split(':', 1)[1]
            return (ROOT / relative).read_bytes()
        self.fixture.run(main, git_payload=content)
        package = self.fixture.arguments['output'] / 'New-Horizons-Linux-x64'
        identity = verify_candidate(package)
        self.assertEqual(set(identity['setup']), {SETUP_NAME, *SUPPORT})
        self.assertIn(SETUP_NAME, identity['packaged_elfs'])
        self.assertEqual((package / SETUP_NAME).stat().st_mode & 0o222, 0)
        assets = self.root / 'original Complete'
        for name in ('Data', 'Maps', 'Mp3'):
            (assets / name).mkdir(parents=True)
        for name in ('h3bitmap.lod', 'h3sprite.lod', 'h3ab_bmp.lod', 'h3ab_spr.lod'):
            (assets / 'Data' / name).write_bytes(b'synthetic original archive')
        profile = self.root / 'profile'
        result = subprocess.run(['bash', str(package / 'Play-New-Horizons.sh'),
                                 '--assets', str(assets), '--profile', str(profile), '--verify-only'],
                                env={**os.environ, 'NH_PYTHON': sys.executable}, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertFalse(profile.exists())

    def test_missing_setup_import_root_is_rejected(self):
        del self.fixture.elf['shipped_strong_dynamic_imports_resolved']['bundled/' + SETUP_NAME]
        del self.fixture.elf['unresolved_strong_imports']['bundled/' + SETUP_NAME]
        with self.assertRaisesRegex(RuntimeError, 'strong-import'):
            self.fixture.run(main)

    def test_arbitrary_third_root_is_rejected(self):
        self.fixture.identity['binaries']['other-program'] = '0' * 64
        self.write_candidate()
        with self.assertRaisesRegex(RuntimeError, 'identity roots'):
            self.fixture.run(main)

    def test_missing_fixed_support_is_rejected(self):
        (self.fixture.candidate / 'linux_assets_setup.py').unlink()
        self.write_candidate()
        with self.assertRaisesRegex(RuntimeError, 'incomplete'):
            self.fixture.run(main)


if __name__ == '__main__':
    unittest.main()
