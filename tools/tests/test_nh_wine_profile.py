# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic content contracts only: no original assets or Wine execution."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("nh_wine_profile", Path(__file__).with_name("nh_wine_profile.py"))
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)


class WineProfileTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.root = self.base / "run"
        self.root.mkdir(mode=0o700)
        self.original = {}
        for group in ("data", "music"):
            path = self.base / group
            path.mkdir()
            file = path / "synthetic.bin"
            file.write_bytes(group.encode())
            self.original[group] = {"path": str(path), "files": {file.name: self.sha(file)}}
        self.map = self.base / "fixture.vmap"
        self.map.write_bytes(b"synthetic map")
        self.settings = self.base / "settings.json"
        self.settings.write_text('{"video":{}}')
        self.preset = self.base / "preset.json"
        self.preset.write_text(json.dumps({"activePreset": "default", "presets": {"default": {
            "mods": ["vcmi", "core", "new-horizons"], "settings": {}}}}))
        self.manifest = self.base / "content.json"
        self.row = {"version": 1, "original": self.original,
                    "map": {**self.pin(self.map), "filename": "fixture.vmap"},
                    "settings": self.pin(self.settings), "modPreset": self.pin(self.preset)}

    def tearDown(self):
        self.temp.cleanup()

    def sha(self, path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def pin(self, path):
        return {"path": str(path), "sha256": self.sha(path)}

    def load(self):
        self.manifest.write_text(json.dumps(self.row))
        return PROFILE.load_content_manifest(self.manifest, self.sha(self.manifest))

    def test_read_only_admission_and_explicit_staging(self):
        content = self.load()
        self.assertEqual(list(self.root.iterdir()), [])
        PROFILE.stage_content(content, self.root)
        PROFILE.verify_staged_content(content, self.root)
        PROFILE.verify_original_content(content)
        profile = self.root / "localappdata/HeroesIII-NewHorizons"
        self.assertEqual((profile / "content/Maps/fixture.vmap").read_bytes(), self.map.read_bytes())
        self.assertEqual((profile / "content/Data").readlink(), Path(self.original["data"]["path"]))
        self.assertEqual((profile / "content/Mp3").readlink(), Path(self.original["music"]["path"]))
        self.assertFalse((profile / "Saves").exists())
        with self.assertRaises(FileExistsError):
            PROFILE.stage_content(content, self.root)

    def test_mutable_settings_but_immutable_map_and_originals(self):
        content = self.load()
        PROFILE.stage_content(content, self.root)
        profile = self.root / "localappdata/HeroesIII-NewHorizons"
        (profile / "config/settings.json").write_text('{}')
        PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root)
        (profile / "content/Maps/fixture.vmap").write_bytes(b"changed")
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)
        (self.base / "data/synthetic.bin").write_bytes(b"changed")
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_original_content(content)

    def test_schema_unknown_empty_and_version_refused(self):
        for edit in (lambda r: r.update(extra=1), lambda r: r.update(version=True),
                     lambda r: r["original"]["data"].update(files={}),
                     lambda r: r["settings"].update(extra=1)):
            saved = json.loads(json.dumps(self.row))
            edit(self.row)
            with self.assertRaises(PROFILE.Refusal):
                self.load()
            self.row = saved

    def test_duplicate_fields_bad_hash_and_bounded_metadata(self):
        self.manifest.write_text('{"version":1,"version":1}')
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.load_content_manifest(self.manifest, self.sha(self.manifest))
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.load_content_manifest(self.manifest, "0" * 64)
        self.settings.write_bytes(b"x" * (PROFILE.METADATA_LIMIT + 1))
        self.row["settings"] = self.pin(self.settings)
        with self.assertRaises(PROFILE.Refusal):
            self.load()
        self.assertEqual(list(self.root.iterdir()), [])

    def test_relative_names_and_map_basename_refused(self):
        for name in ("../a", "/a", "a//b", "a/./b", "a\\b", "C:a", "a\x00b"):
            with self.assertRaises(PROFILE.Refusal):
                PROFILE._relative(name)
        for name in ("nested/a.vmap", "bad.txt", "../a.h3m"):
            self.row["map"]["filename"] = name
            with self.assertRaises(PROFILE.Refusal):
                self.load()

    def test_symlink_sources_and_ancestors_refused(self):
        alias = self.base / "alias"
        alias.symlink_to(self.base / "data", target_is_directory=True)
        self.row["original"]["data"]["path"] = str(alias)
        with self.assertRaises(PROFILE.Refusal):
            self.load()
        self.row["original"]["data"]["path"] = str(self.base / "data")
        link = self.base / "linked.vmap"
        link.symlink_to(self.map)
        self.row["map"]["path"] = str(link)
        with self.assertRaises(PROFILE.Refusal):
            self.load()

    def test_settings_and_curated_preset_shape(self):
        for key, path, body in (("settings", self.settings, '[]'),
                                ("modPreset", self.preset, '{"activePreset":"other"}')):
            path.write_text(body)
            self.row[key] = self.pin(path)
            with self.assertRaises(PROFILE.Refusal):
                self.load()

    def test_changed_links_and_private_directory_modes(self):
        content = self.load()
        PROFILE.stage_content(content, self.root)
        self.root.chmod(0o755)
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)
        self.root.chmod(0o700)
        link = self.root / "localappdata/HeroesIII-NewHorizons/content/Data"
        link.unlink()
        link.symlink_to(self.base / "music")
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)
