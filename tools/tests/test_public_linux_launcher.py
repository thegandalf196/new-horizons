"""Synthetic public-package launcher checks; never execute a game binary."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class PublicLinuxLauncherTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nh public launcher ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.package = self.work / "extracted package"
        self.package.mkdir()
        shutil.copy2(ROOT / "tools/Play-New-Horizons.sh", self.package)
        self.environment = dict(os.environ, HOME=str(self.work / "home"))

    def run_wrapper(self, *arguments):
        return subprocess.run(
            ["bash", str(self.package / "Play-New-Horizons.sh"), *arguments],
            cwd=self.work, env=self.environment, capture_output=True, timeout=10)

    def prepare_preflight(self, binary):
        shutil.copy2(ROOT / "tools/new-horizons-launch.sh", self.package)
        assets = self.work / "original Complete"
        for name in ("Data", "Maps", "Mp3"):
            (assets / name).mkdir(parents=True)
        # The preflight checks presence only; these contain no original assets.
        for name in ("h3bitmap.lod", "h3sprite.lod"):
            (assets / "Data" / name).write_text("synthetic presence fixture")
        for name, contents in (
                ("config/filesystem.json", "{}"),
                ("Mods/vcmi/mod.json", "{}"),
                ("scripts/damage/damageCalculator.lua", "return {}"),
                ("libvcmi.so", "synthetic library, not an ELF")):
            path = self.package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        # Execution would leave evidence and fail. --verify-only must never run it.
        marker = self.work / "client-was-executed"
        path = self.package / binary
        path.write_text('#!/bin/sh\ntouch "$PUBLIC_LAUNCH_TEST_MARKER"\nexit 91\n')
        path.chmod(0o755)
        self.environment["PUBLIC_LAUNCH_TEST_MARKER"] = str(marker)
        return assets, self.work / "new profile", marker

    def test_current_client_preflight_does_not_launch_or_create_profile(self):
        assets, profile, marker = self.prepare_preflight("new-horizons")
        result = self.run_wrapper("--assets", str(assets), "--profile", str(profile), "--verify-only")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertFalse(marker.exists())
        self.assertFalse(profile.exists())

    def test_old_filename_is_not_a_silent_fallback(self):
        assets, profile, marker = self.prepare_preflight("vcmiclient")
        result = self.run_wrapper("--assets", str(assets), "--profile", str(profile), "--verify-only")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Client is not an executable file", result.stderr)
        self.assertFalse(marker.exists())
        self.assertFalse(profile.exists())

    def test_package_paths_and_user_arguments_are_forwarded_verbatim(self):
        helper = self.package / "new-horizons-launch.sh"
        helper.write_text('#!/bin/bash\nprintf "%s\\0" "$@"\n')
        arguments = ["--assets", "/original path", "--profile", "/profile path",
                     "--verify-only", "--", "--testmap", "Maps/Map with spaces.h3m"]
        result = self.run_wrapper(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        expected = ["--client", str(self.package / "new-horizons"),
                    "--resources", str(self.package), *arguments]
        self.assertEqual(result.stdout.decode().split("\0")[:-1], expected)


if __name__ == "__main__":
    unittest.main()
