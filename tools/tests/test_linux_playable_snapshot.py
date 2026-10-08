"""Integrity checks for the local Linux playable snapshot helper."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))
from linux_playable_snapshot import (freeze, make_removable, package_files, promote,
                                     resolve, verify_snapshot, write_metadata)


class LinuxPlayableSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "build" / "bin"
        self.bin.mkdir(parents=True)
        self.client = self.bin / "vcmiclient"
        self.client.write_text("#!/bin/sh\nexit 0\n")
        self.client.chmod(0o755)
        (self.bin / "libvcmi.so").write_bytes(b"matching library")
        self.source = self.root / "source"
        for relative in ("config", "scripts/damage", "Mods/vcmi", "Mods/new-horizons/Images"):
            (self.source / relative).mkdir(parents=True, exist_ok=True)
        for relative, content in (
            ("config/filesystem.json", "{}"),
            ("config/newHorizonsCombat.json", "{}"),
            ("config/newHorizonsMagic.json", "{}"),
            ("scripts/damage/damageCalculator.lua", "return {}"),
            ("Mods/vcmi/mod.json", "{}"),
            ("Mods/new-horizons/mod.json", "{}"),
            ("Mods/new-horizons/Images/icon.png", "synthetic image"),
        ):
            (self.source / relative).write_text(content)
        self.resources = self.root / "build" / "resources"
        self.resources.mkdir()
        for name in ("config", "scripts", "Mods"):
            (self.resources / name).symlink_to(self.source / name, target_is_directory=True)
        self.store = self.root / "playable-snapshots"

    def tearDown(self):
        if self.store.exists():
            for path in self.store.glob("snapshot-*"):
                if path.is_dir() and not path.is_symlink():
                    make_removable(path)

    def freeze_candidate(self, retain_resources_from=None):
        output = io.StringIO()
        with redirect_stdout(output):
            freeze(SimpleNamespace(client=self.client, resources=self.resources,
                                   store=self.store, promote=False,
                                   retain_resources_from=retain_resources_from))
        return Path(output.getvalue().strip())

    def promote_candidate(self, snapshot):
        output = io.StringIO()
        with redirect_stdout(output):
            promote(SimpleNamespace(snapshot=snapshot, store=self.store))
        return Path(output.getvalue().strip())

    def selected_snapshot(self):
        output = io.StringIO()
        with redirect_stdout(output):
            resolve(SimpleNamespace(store=self.store))
        return Path(output.getvalue().strip())

    def test_requires_explicit_snapshot_and_does_not_fall_back_to_build_bin(self):
        with self.assertRaisesRegex(RuntimeError, "No frozen Linux playable snapshot"):
            self.selected_snapshot()
        self.assertTrue(self.client.exists())

    def test_copy_is_immutable_from_source_edits_and_checksum_verified(self):
        snapshot = self.freeze_candidate()
        with self.assertRaisesRegex(RuntimeError, "No frozen Linux playable snapshot"):
            self.selected_snapshot()
        self.promote_candidate(snapshot)
        original = (snapshot / "config/filesystem.json").read_text()
        (self.source / "config/filesystem.json").write_text('{"active": true}')
        self.assertEqual((snapshot / "config/filesystem.json").read_text(), original)
        self.assertEqual(self.selected_snapshot(), snapshot)
        self.assertEqual(verify_snapshot(snapshot)["snapshot_sha256"], snapshot.name.removeprefix("snapshot-"))
        self.assertFalse((snapshot / "config").is_symlink())
        self.assertFalse((snapshot / "Mods/new-horizons").is_symlink())

        writable = snapshot / "config/filesystem.json"
        writable.chmod(0o644)
        writable.write_text("changed")
        with self.assertRaisesRegex(RuntimeError, "inventory or checksum mismatch"):
            self.selected_snapshot()

    def test_promotions_retain_every_snapshot_that_may_be_in_use(self):
        first = self.freeze_candidate()
        self.promote_candidate(first)
        unrelated = self.store / "operator-notes"
        unrelated.mkdir()
        (unrelated / "keep.txt").write_text("leave me")

        (self.source / "config/filesystem.json").write_text('{"candidate": 2}')
        second = self.freeze_candidate()
        self.assertNotEqual(first, second)
        self.assertEqual(self.selected_snapshot(), first)
        self.assertTrue(first.is_dir())
        self.promote_candidate(second)
        self.assertEqual(self.selected_snapshot(), second)

        (self.source / "config/filesystem.json").write_text('{"candidate": 3}')
        third = self.freeze_candidate()
        self.assertNotEqual(second, third)
        self.assertEqual(self.selected_snapshot(), second)
        self.promote_candidate(third)
        self.assertEqual(self.selected_snapshot(), third)
        self.promote_candidate(third)
        snapshots = sorted(path for path in self.store.glob("snapshot-*") if path.is_dir())
        self.assertEqual(set(snapshots), {first, second, third})
        pointer = json.loads((self.store / "current.json").read_text())
        self.assertEqual(pointer["previous_snapshot"], second.name)
        self.assertEqual((unrelated / "keep.txt").read_text(), "leave me")

    def test_nested_source_symlink_is_rejected(self):
        link = self.source / "config" / "elsewhere"
        link.symlink_to(self.root / "outside", target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "symlink or non-regular file"):
            self.freeze_candidate()

    def test_current_pointer_is_atomic_json_identity(self):
        snapshot = self.freeze_candidate()
        self.promote_candidate(snapshot)
        pointer = json.loads((self.store / "current.json").read_text())
        self.assertEqual(pointer["snapshot"], snapshot.name)
        self.assertEqual(pointer["snapshot_sha256"], snapshot.name.removeprefix("snapshot-"))
        self.assertFalse((self.store / "current.json").is_symlink())

    def test_corrupt_candidate_cannot_be_promoted(self):
        snapshot = self.freeze_candidate()
        with self.assertRaisesRegex(RuntimeError, "inventory or checksum mismatch"):
            file = snapshot / "config/filesystem.json"
            file.chmod(0o644)
            file.write_text("tampered")
            self.promote_candidate(snapshot)
        with self.assertRaisesRegex(RuntimeError, "No frozen Linux playable snapshot"):
            self.selected_snapshot()

    def add_optional_magic_resources(self):
        manifest = self.source / "config/newHorizonsMagicAssets.json"
        image = self.source / "Mods/new-horizons/Images/NH_magic_assets/casting/nature/frame.png"
        image.parent.mkdir(parents=True)
        manifest.write_text('{"casting": "NH_magic_assets/casting/nature/frame.png"}')
        image.write_bytes(b"synthetic optional casting art")
        return manifest, image

    def test_retention_blocks_optional_manifest_and_art_omissions_before_copy(self):
        manifest, image = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        self.promote_candidate(baseline)
        manifest.unlink()
        image.unlink()
        with self.assertRaisesRegex(RuntimeError, "omits retained baseline resources") as failure:
            self.freeze_candidate(baseline)
        self.assertIn("config/newHorizonsMagicAssets.json", str(failure.exception))
        self.assertIn("Images/NH_magic_assets/casting/nature/frame.png", str(failure.exception))
        self.assertEqual(self.selected_snapshot(), baseline)
        self.assertEqual(list(self.store.glob("snapshot-*")), [baseline])
        self.assertFalse(list(self.store.glob(".snapshot-*")))
        self.assertFalse(manifest.exists(), "Guard must not auto-copy old configuration")
        self.assertFalse(image.exists(), "Guard must not auto-copy old artwork")

    def test_retention_allows_updated_gameplay_and_new_binaries_at_existing_paths(self):
        self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        changed = self.source / "config/newHorizonsMagic.json"
        changed.write_text('{"updatedGameplay": true}')
        self.client.write_text("#!/bin/sh\nexit 1\n")
        (self.bin / "libvcmi.so").write_bytes(b"new matching library")
        candidate = self.freeze_candidate(baseline)
        self.assertNotEqual(candidate, baseline)
        self.assertEqual((candidate / "config/newHorizonsMagic.json").read_text(), changed.read_text())
        self.assertEqual((candidate / "vcmiclient").read_text(), self.client.read_text())
        self.assertEqual((candidate / "libvcmi.so").read_bytes(), b"new matching library")
        verify_snapshot(candidate)

    def test_retention_rejects_tampered_baseline(self):
        baseline = self.freeze_candidate()
        image = baseline / "Mods/new-horizons/Images/icon.png"
        image.chmod(0o644)
        image.write_bytes(b"tampered baseline")
        with self.assertRaisesRegex(RuntimeError, "inventory or checksum mismatch"):
            self.freeze_candidate(baseline)
        self.assertFalse(list(self.store.glob(".snapshot-*")))

    def test_retention_rejects_baseline_symlink(self):
        baseline = self.freeze_candidate()
        link = self.root / "baseline-link"
        link.symlink_to(baseline, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "missing or is a symlink"):
            self.freeze_candidate(link)

    def test_retention_checks_only_curated_resource_roots(self):
        baseline = self.freeze_candidate()
        make_removable(baseline)
        (baseline / "operator-notes.txt").write_text("Verified baseline note, not a resource")
        digest = write_metadata(baseline, package_files(baseline))
        renamed = self.store / ("snapshot-" + digest)
        baseline.rename(renamed)
        verify_snapshot(renamed)
        candidate = self.freeze_candidate(renamed)
        self.assertFalse((candidate / "operator-notes.txt").exists())
        verify_snapshot(candidate)

    def test_without_retention_option_preserves_existing_omission_behavior(self):
        manifest, image = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        manifest.unlink()
        image.unlink()
        candidate = self.freeze_candidate()
        self.assertNotEqual(candidate, baseline)
        self.assertFalse((candidate / "config/newHorizonsMagicAssets.json").exists())
        verify_snapshot(candidate)

    def test_cli_retention_option_reports_missing_optional_resources(self):
        manifest, _ = self.add_optional_magic_resources()
        baseline = self.freeze_candidate()
        manifest.unlink()
        script = Path(__file__).resolve().parents[1] / "ci/linux_playable_snapshot.py"
        result = subprocess.run([sys.executable, str(script), "freeze", "--client", str(self.client),
                                 "--resources", str(self.resources), "--store", str(self.store),
                                 "--retain-resources-from", str(baseline)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("config/newHorizonsMagicAssets.json", result.stderr)


if __name__ == "__main__":
    unittest.main()
