#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows system-provider classification; synthetic graph, no Conan/network/PE execution."""
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "ci/package_new_horizons_windows.py"
spec = importlib.util.spec_from_file_location("nh_package", SOURCE)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class SystemDependencyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        (self.cache / "conaninfo.txt").write_text("synthetic metadata")
        (self.cache / "conanmanifest.txt").write_text("synthetic manifest")
        self.package = self.root / "package"
        self.package.mkdir()
        self.node = {
            "ref": "opengl/system#cfcf523b9d2bad75cbf377f56562634c",
            "settings": {"os": "Windows", "arch": "x86_64"},
            "package_folder": str(self.cache), "recipe_folder": str(self.cache),
            "context": "host", "license": "MIT",
        }

    def collect(self):
        graph = self.root / "graph.json"
        graph.write_text(json.dumps({"graph": {"nodes": {"1": self.node}}}))
        archive = self.root / "sources.tar.gz"
        with patch.object(packager.subprocess, "run", side_effect=AssertionError("No source fetch for OS implementation")):
            packager.collect_notices(graph, self.package, archive)
        return archive

    def test_system_provider_kept_in_manifest_without_fake_sources(self):
        archive = self.collect()
        metadata = json.loads((self.package / "DEPENDENCIES.json").read_text())
        self.assertEqual(len(metadata), 1)
        self.assertTrue(metadata[0]["system_only"])
        self.assertEqual(metadata[0]["system_libraries"], ["opengl32.dll"])
        self.assertEqual(metadata[0]["reference"], self.node["ref"])
        with tarfile.open(archive) as sources:
            self.assertEqual(sources.getnames(), [])

    def test_real_dependency_missing_notices_still_fails(self):
        self.node["ref"] = "actual-library/1.0#abc"
        with self.assertRaisesRegex(RuntimeError, "Missing dependency license texts"):
            self.collect()

    def test_other_system_provider_not_silently_exempted(self):
        self.node["ref"] = "other/system#abc"
        with self.assertRaisesRegex(RuntimeError, "Missing dependency license texts"):
            self.collect()

    def test_unreviewed_opengl_revision_not_exempted(self):
        self.node["ref"] = "opengl/system#changed"
        with self.assertRaisesRegex(RuntimeError, "Missing dependency license texts"):
            self.collect()

    def test_non_windows_provider_not_exempted(self):
        self.node["settings"]["os"] = "Linux"
        with self.assertRaisesRegex(RuntimeError, "Missing dependency license texts"):
            self.collect()

    def test_unexpected_payload_fails_even_for_pinned_provider(self):
        for name in ("opengl32.dll", "implementation.lib", "header.h"):
            path = self.cache / name
            path.write_bytes(b"not a system metadata file")
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Unexpected payload"):
                packager.windows_system_dependency(self.node)
            path.unlink()


if __name__ == "__main__":
    unittest.main()
