#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Corresponding source/notices, including skipped static host recipes; no network."""
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "ci/package_new_horizons_windows.py"
spec = importlib.util.spec_from_file_location("nh_notices", SOURCE)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class DependencyNoticesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "package"
        self.package.mkdir()
        self.recipe = self.root / "recipe"
        self.recipe.mkdir()
        (self.recipe / "conanfile.py").write_text("# synthetic pinned recipe")
        (self.recipe / "LICENSE").write_text("MIT license for recipe, NOT upstream")
        self.node = {
            "ref": "dav1d/1.5.4#001b758cfd88fd816b286e09e686dd5c",
            "context": "host", "binary": "Skip", "package_folder": None,
            "recipe_folder": str(self.recipe), "license": "BSD-2-Clause",
        }
        self.source_files = {"src/COPYING": b"Synthetic upstream BSD license text"}
        self.commands = []
        self.exported = None
        self.restore_exports = True

    def run_conan(self, command, **kwargs):
        self.commands.append(command)
        if command[1:3] == ["cache", "path"]:
            if self.exported is not None:
                return subprocess.CompletedProcess(command, 0, str(self.exported), "")
            return subprocess.CompletedProcess(command, 1, "", "export_source folder does not exist")
        if command[1] == "download":
            self.assertEqual(command[2:], [self.node["ref"], "--only-recipe", "-r", "conancenter"])
            if self.restore_exports:
                (self.exported / "fix.patch").write_bytes(b"exact exported patch")
            return subprocess.CompletedProcess(command, 0)
        self.assertEqual(command[1], "source")
        for name, content in self.source_files.items():
            path = Path(command[2]) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        return subprocess.CompletedProcess(command, 0)

    def collect(self, extra=None):
        graph = self.root / "graph.json"
        graph.write_text(json.dumps({"graph": {"nodes": {"1": self.node, **(extra or {})}}}))
        archive = self.root / "sources.tar.gz"
        with patch.object(packager.subprocess, "run", side_effect=self.run_conan):
            packager.collect_notices(graph, self.package, archive)
        return json.loads((self.package / "DEPENDENCIES.json").read_text()), archive

    def test_skipped_static_has_manifest_actual_notice_and_source(self):
        metadata, archive = self.collect()
        self.assertEqual(len(metadata), 1)
        self.assertEqual(metadata[0]["reference"], self.node["ref"])
        self.assertEqual(metadata[0]["source_archive_directory"], "dav1d_1.5.4")
        self.assertEqual(len(metadata[0]["notices"]), 1)
        self.assertEqual((self.package / metadata[0]["notices"][0]).read_bytes(), self.source_files["src/COPYING"])
        self.assertEqual(self.commands[0][3], self.node["ref"])
        with tarfile.open(archive) as sources:
            self.assertEqual(sources.extractfile("dav1d_1.5.4/src/COPYING").read(), self.source_files["src/COPYING"])

    def test_recipe_mit_alone_does_not_satisfy_missing_upstream_notice(self):
        self.source_files = {}
        with self.assertRaisesRegex(RuntimeError, "Missing dependency license texts"):
            self.collect()
        self.assertTrue(any(command[1] == "source" for command in self.commands))
        self.assertFalse((self.package / "DEPENDENCIES.json").exists())

    def test_empty_upstream_license_still_fails(self):
        self.source_files = {"src/COPYING": b""}
        with self.assertRaisesRegex(RuntimeError, "Missing dependency license texts"):
            self.collect()

    def test_source_can_replace_recipe_root_license(self):
        self.source_files = {"LICENSE": b"Actual upstream license replacing recipe license"}
        metadata, _ = self.collect()
        self.assertEqual((self.package / metadata[0]["notices"][0]).read_bytes(), self.source_files["LICENSE"])

    def test_ffmpeg_full_license_supplements_package_explanation(self):
        self.node["ref"] = "ffmpeg/6.1.1#synthetic"
        binary = self.root / "binary" / "licenses"
        binary.mkdir(parents=True)
        (binary / "LICENSE.md").write_text("See COPYING.LGPLv2.1 for actual terms")
        self.node["package_folder"] = str(binary.parent)
        self.source_files = {"src/COPYING.LGPLv2.1": b"Synthetic complete LGPL fixture text"}
        metadata, archive = self.collect()
        notices = [self.package / name for name in metadata[0]["notices"]]
        self.assertEqual({path.name for path in notices}, {"LICENSE.md", "COPYING.LGPLv2.1"})
        full = next(path for path in notices if path.name == "COPYING.LGPLv2.1")
        self.assertEqual(full.read_bytes(), self.source_files["src/COPYING.LGPLv2.1"])
        with tarfile.open(archive) as sources:
            self.assertIn("ffmpeg_6.1.1/src/COPYING.LGPLv2.1", sources.getnames())

    def test_ffmpeg_reference_document_without_terms_fails(self):
        self.node["ref"] = "ffmpeg/6.1.1#synthetic"
        self.source_files = {"src/LICENSE.md": b"See COPYING.LGPLv2.1"}
        with self.assertRaisesRegex(RuntimeError, "Missing FFmpeg source license text"):
            self.collect()

    def test_sqlite_embedded_notice_without_license_file(self):
        self.node['ref'] = 'sqlite3/3.53.4#89fcf5cda598966acb7f3e185b19c58d'
        notice = (b'/*\n** The author disclaims copyright to this source code. In place of\n'
                  b'** a legal notice, here is a blessing:\n'
                  b'** May you do good and not evil.\n'
                  b'** May you find forgiveness for yourself and forgive others.\n'
                  b'** May you share freely, never taking more than you give.\n*/')
        self.source_files = {'src/sqlite3.h': notice + b'\n/* untouched API declarations fixture */\n'}
        metadata, archive = self.collect()
        self.assertEqual((self.package / metadata[0]['notices'][0]).read_bytes(), notice)
        self.assertEqual(metadata[0]['notice_provenance']['source_file_sha256'],
                         hashlib.sha256(self.source_files['src/sqlite3.h']).hexdigest())
        with tarfile.open(archive) as sources:
            self.assertEqual(sources.extractfile('sqlite3_3.53.4/src/sqlite3.h').read(), self.source_files['src/sqlite3.h'])

    def test_sqlite_missing_embedded_notice_does_not_accept_recipe_mit(self):
        self.node['ref'] = 'sqlite3/3.53.4#synthetic'
        self.source_files = {'src/sqlite3.h': b'/* API declarations, no dedication */'}
        with self.assertRaisesRegex(RuntimeError, 'Missing verified SQLite'):
            self.collect()

    def test_sqlite_partial_blessing_is_not_a_verified_notice(self):
        self.node['ref'] = 'sqlite3/3.53.4#synthetic'
        self.source_files = {'src/sqlite3.h': b'/* The author disclaims copyright to this source code. */'}
        with self.assertRaisesRegex(RuntimeError, 'Missing verified SQLite'):
            self.collect()

    def test_root_build_and_qt_excluded_but_skipped_host_retained(self):
        metadata, _ = self.collect({
            "0": {**self.node, "ref": "consumer/1.0"},
            "2": {**self.node, "ref": "tool/1.0", "context": "build"},
            "3": {**self.node, "ref": "qt/6.0"},
        })
        self.assertEqual([item["reference"] for item in metadata], [self.node["ref"]])
        self.assertEqual(len(self.commands), 2)

    def prepare_missing_export(self):
        self.exported = self.root / "exports"
        self.exported.mkdir()
        checksum = hashlib.md5(b"exact exported patch", usedforsecurity=False).hexdigest()
        (self.recipe / "conanmanifest.txt").write_text("1\nexport_source/fix.patch: " + checksum + "\n")

    def test_missing_exported_patch_restored_at_exact_revision_before_source(self):
        self.prepare_missing_export()
        _, archive = self.collect()
        self.assertEqual([command[1] for command in self.commands], ["cache", "download", "cache", "source"])
        with tarfile.open(archive) as sources:
            self.assertEqual(sources.extractfile("dav1d_1.5.4/fix.patch").read(), b"exact exported patch")

    def test_unresolved_exported_patch_fails_before_source(self):
        self.prepare_missing_export()
        self.restore_exports = False
        with self.assertRaisesRegex(RuntimeError, "Missing or mismatched exact exported sources"):
            self.collect()
        self.assertFalse(any(command[1] == "source" for command in self.commands))

    def test_missing_exact_recipe_fails_closed(self):
        self.node["recipe_folder"] = None
        with self.assertRaisesRegex(RuntimeError, "Missing exact cached recipe"):
            self.collect()


if __name__ == "__main__":
    unittest.main()
