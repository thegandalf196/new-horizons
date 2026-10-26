# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic content contracts only: no original assets or Wine execution."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

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
        self.assertFalse((profile / "content/Saves").exists())
        self.assertNotIn("inputSave", content)
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


    def add_input_save(self):
        self.save = self.base / "earned.vsgm1"
        self.save.write_bytes(b"synthetic earned save")
        self.row["inputSave"] = {**self.pin(self.save), "filename": "MatchedStart.vsgm1"}

    def test_input_save_uses_actual_windows_user_save_path_not_content_mount(self):
        dirs_path = Path(__file__).resolve().parents[2] / "tools/windows/dirs.json"
        shipped = json.loads(dirs_path.read_text())["userSavePath"]
        parts = shipped.split("\\")
        self.assertEqual(parts[0], "%LOCALAPPDATA%")
        expected = self.root / "localappdata" / Path(*parts[1:])
        self.add_input_save()
        content = self.load()
        # The real owner creates this exact owned directory before content staging.
        expected.mkdir(parents=True, mode=0o700)
        for directory in expected.parents:
            if directory == self.root:
                break
            directory.chmod(0o700)
        PROFILE.stage_content(content, self.root)
        self.assertEqual(PROFILE._destinations(self.root)[2], expected)
        self.assertEqual((expected / "MatchedStart.vsgm1").read_bytes(), self.save.read_bytes())
        self.assertFalse((self.root / "localappdata/HeroesIII-NewHorizons/content/Saves").exists())
        PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)

    def test_input_save_user_save_directory_and_ancestors_cannot_redirect(self):
        self.add_input_save()
        content = self.load()
        save_root = self.root / "localappdata/new-horizons"
        save_root.mkdir(parents=True, mode=0o700)
        (self.root / "localappdata").chmod(0o700)
        outside = self.base / "outside"
        outside.mkdir(mode=0o700)
        (save_root / "Saves").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.stage_content(content, self.root)
        self.assertEqual(list(outside.iterdir()), [])

    def test_optional_save_staging_leaves_source_and_distinct_output_independent(self):
        self.add_input_save()
        source_before = self.save.read_bytes()
        content = self.load()
        self.assertEqual(list(self.root.iterdir()), [])
        PROFILE.stage_content(content, self.root)
        saves = self.root / "localappdata/new-horizons/Saves"
        self.assertEqual((saves / "MatchedStart.vsgm1").read_bytes(), source_before)
        (saves / "NewOutput.vsgm1").write_bytes(b"ordinary distinct save")
        PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)
        PROFILE.verify_original_content(content)
        self.assertEqual(self.save.read_bytes(), source_before)

    def test_input_save_source_tamper_refused_before_copy_and_each_input_and_final(self):
        self.add_input_save()
        content = self.load()
        original = self.save.read_bytes()
        self.save.write_bytes(b"changed")
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.stage_content(content, self.root)
        self.save.write_bytes(original)
        # The refused copy may create private directories/links, so use a fresh root.
        second = self.base / "second-run"
        second.mkdir(mode=0o700)
        PROFILE.stage_content(content, second)
        self.save.write_bytes(b"changed again")
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, second, verify_copy_bytes=False)
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_original_content(content)

    def test_input_save_staged_tamper_and_symlink_refused_after_input(self):
        self.add_input_save()
        content = self.load()
        PROFILE.stage_content(content, self.root)
        staged = self.root / "localappdata/new-horizons/Saves/MatchedStart.vsgm1"
        staged.write_bytes(b"overwritten input save")
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)
        staged.unlink()
        staged.symlink_to(self.save)
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)

    def test_input_save_exact_schema_and_plain_basename(self):
        self.add_input_save()
        valid = dict(self.row["inputSave"])
        malformed = [None, [], {}, {**valid, "extra": 1},
                     {k: v for k, v in valid.items() if k != "sha256"}]
        malformed += [{**valid, "filename": name} for name in
                      ("../a.vsgm1", "nested/a.vsgm1", "/a.vsgm1", "a\\b.vsgm1",
                       "C:a.vsgm1", "a.h3m", "a.vsgm1\n", "a\x00.vsgm1")]
        for row in malformed:
            with self.subTest(row=row):
                self.row["inputSave"] = row
                with self.assertRaises(PROFILE.Refusal):
                    self.load()
        self.assertEqual(list(self.root.iterdir()), [])

    def test_input_save_source_symlink_and_ancestor_and_hash_refused(self):
        self.add_input_save()
        valid = dict(self.row["inputSave"])
        linked = self.base / "linked.vsgm1"
        linked.symlink_to(self.save)
        ancestor = self.base / "alias"
        ancestor.symlink_to(self.base, target_is_directory=True)
        for path in (linked, ancestor / self.save.name):
            self.row["inputSave"] = {**valid, "path": str(path)}
            with self.assertRaises(PROFILE.Refusal):
                self.load()
        self.row["inputSave"] = {**valid, "sha256": "0" * 64}
        with self.assertRaises(PROFILE.Refusal):
            self.load()

    def test_input_save_size_boundary_and_staged_directory_mode(self):
        self.add_input_save()
        self.assertEqual(PROFILE.SAVE_LIMIT, 128 * 1024 * 1024)
        with mock.patch.object(PROFILE, "SAVE_LIMIT", len(self.save.read_bytes()) - 1):
            with self.assertRaises(PROFILE.Refusal):
                self.load()
        with mock.patch.object(PROFILE, "SAVE_LIMIT", len(self.save.read_bytes())):
            content = self.load()
            PROFILE.stage_content(content, self.root)
        saves = self.root / "localappdata/new-horizons/Saves"
        saves.chmod(0o755)
        with self.assertRaises(PROFILE.Refusal):
            PROFILE.verify_staged_content(content, self.root, verify_copy_bytes=False)

    def test_input_save_existing_destination_is_never_overwritten(self):
        self.add_input_save()
        content = self.load()
        saves = self.root / "localappdata/new-horizons/Saves"
        saves.mkdir(parents=True, mode=0o700)
        for directory in saves.parents:
            if directory == self.root:
                break
            directory.chmod(0o700)
        staged = saves / "MatchedStart.vsgm1"
        staged.write_bytes(b"existing")
        with self.assertRaises(FileExistsError):
            PROFILE.stage_content(content, self.root)
        self.assertEqual(staged.read_bytes(), b"existing")
