"""Verify Linux public archive structure and writable player root."""
# SPDX-License-Identifier: GPL-2.0-or-later

import hashlib
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import archive_linux_player_release as packaging
from PortableReleaseUpdate import PLAYER_EXECUTABLES, PLAYER_FILES


class LinuxPlayerReleaseArchiveTest(unittest.TestCase):
    def test_archive_keeps_package_writable_without_changing_frozen_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / packaging.PACKAGE_ROOT
            for name in PLAYER_FILES:
                entry = stage / name
                entry.parent.mkdir(parents=True, exist_ok=True)
                entry.write_bytes(b'NHARTfixture' if entry.suffix == '.nhart' else name.encode())
                entry.chmod(0o555 if name in PLAYER_EXECUTABLES else 0o444)
            try:
                stage.chmod(0o555)
                with patch.object(packaging, 'verify_candidate', return_value={'source_commit': 'a' * 40}):
                    archive, digest = packaging.archive_stage(stage, root)
                    with self.assertRaises(FileExistsError):
                        packaging.archive_stage(stage, root)
                self.assertEqual(archive.name, packaging.PACKAGE_ROOT + '-aaaaaaaaaaaa.tar.gz')
                self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), digest)
                self.assertEqual(stage.stat().st_mode & 0o777, 0o555)
                with tarfile.open(archive) as contents:
                    self.assertEqual(contents.getmember(packaging.PACKAGE_ROOT).mode, 0o755)
                    launcher = contents.getmember(packaging.PACKAGE_ROOT + '/Play-New-Horizons.sh')
                    self.assertEqual(launcher.mode, 0o755)
                    self.assertEqual(contents.extractfile(launcher).read(), b'Play-New-Horizons.sh')
            finally:
                stage.chmod(0o755)


if __name__ == '__main__':
    unittest.main()
