"""Validate the detached Cabir preview algorithm with synthetic historical inputs.

Selected v3 shipping artwork is verified by the Cabir NHART tests; historical
v1/v2 drafts are private authoring material, not fresh-checkout dependencies.
"""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_cabir_ingame_preview as preview


class CabirIngamePreviewTest(unittest.TestCase):
    def setUp(self):
        self.fixture_dir = tempfile.TemporaryDirectory(prefix="nh-cabir-preview-source-")
        self.addCleanup(self.fixture_dir.cleanup)
        self.sources = {}
        for version, color in (("v1", (180, 24, 40, 255)), ("v2", (90, 70, 30, 255))):
            spec = dict(preview.source_spec(version))
            source = Path(self.fixture_dir.name) / (version + ".png")
            image = Image.new("RGBA", (64, 60), (0, 0, 0, 0))
            image.paste(color, spec["alpha_bounds"])
            image.save(source)
            spec["source"] = source
            spec["source_sha256"] = preview._sha256(source.read_bytes())
            if spec["master"] is not None:
                master = Path(self.fixture_dir.name) / (version + "-master.png")
                Image.new("RGBA", (128, 128), color).save(master)
                spec["master"] = master
                spec["master_sha256"] = preview._sha256(master.read_bytes())
            self.sources[version] = spec
        self.source_patch = patch.dict(preview.VERSION_SOURCES, self.sources)
        self.source_patch.start()
        self.addCleanup(self.source_patch.stop)

    def _write_reference_frame(self, path: Path, color, bounds=(170, 100, 250, 320)) -> bytes:
        image = Image.new("RGBA", preview.REFERENCE_CANVAS, (0, 0, 0, 0))
        image.paste(color, bounds)
        image.save(path, format="PNG")
        return path.read_bytes()

    def test_descriptor_covers_every_creature_animation_group(self):
        descriptor = preview.expected_animation_descriptor()
        groups = [sequence["group"] for sequence in descriptor["sequences"]]
        constants = (ROOT / "client/battle/BattleConstants.h").read_text(encoding="utf-8")
        match = re.search(r"enum class ECreatureAnimType\s*\{(.*?)\};", constants, re.S)
        self.assertIsNotNone(match)
        declared = {
            int(value)
            for _name, value in re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)", match.group(1), re.M)
            if value != "-1"
        }
        self.assertEqual(groups, sorted(groups))
        self.assertEqual(set(groups), declared)
        self.assertEqual(len(groups), 32)
        self.assertTrue(all(sequence["frames"] == ["00.png"] for sequence in descriptor["sequences"]))
        self.assertIn(2, groups, "CreatureAnimation requires a Holding group")
        self.assertIn(5, groups, "CreatureAnimation requires a Death group")

        loader = (ROOT / "clientsdl2/render/RenderHandler.cpp").read_text(encoding="utf-8")
        self.assertIn('group["group"].Integer()', loader)
        self.assertIn('group["frames"].Vector()', loader)

    def test_bundle_keeps_gameplay_unchanged_and_places_pinned_synthetic_sprite(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-preview-test-") as temp:
            output_dir = Path(temp) / "overlay"
            preview.write_bundle(output_dir)
            verified = preview.verify_bundle(output_dir)
            self.assertEqual(verified, output_dir.resolve())

            patch = json.loads((output_dir / preview.CREATURES_PATH).read_text(encoding="utf-8"))
            self.assertEqual(set(patch), {"core:gremlin", "core:masterGremlin"})
            for creature in patch.values():
                self.assertEqual(creature, {"graphics": {"animation": preview.ANIMATION_NAME}})

            with Image.open(preview.source_spec("v1")["source"]) as opened:
                source = opened.convert("RGBA")
            expected = Image.new("RGBA", preview.LOGICAL_CANVAS, (0, 0, 0, 0))
            expected.alpha_composite(source, preview.SOURCE_PLACEMENT)
            with Image.open(output_dir / preview.NATIVE_IMAGE) as opened:
                actual = opened.convert("RGBA")
            self.assertEqual(actual.size, (450, 400))
            self.assertEqual(actual.tobytes(), expected.tobytes())
            self.assertEqual(actual.getchannel("A").getbbox(), (169, 211, 224, 268))

            readme = (output_dir / "PREVIEW_README.md").read_text(encoding="utf-8")
            self.assertIn("disposable copy", readme)
            self.assertIn("corpse pose remains standing", readme)
            self.assertIn("gameplay rules are unchanged", readme)
            self.assertIn("user-approved v1 base Cabir standing sprite", readme)

            # Preserve geometry while altering transparent RGB bytes; verify must
            # still reject any pixel-level deviation from the approved export.
            altered = actual.copy()
            altered.putpixel((0, 0), (1, 0, 0, 0))
            altered.save(output_dir / preview.NATIVE_IMAGE)
            with self.assertRaisesRegex(ValueError, "pixels differ"):
                preview.verify_bundle(output_dir)

    def test_v2_bundle_uses_pinned_synthetic_input_with_same_center_and_feet_baseline(self):
        spec = preview.source_spec("v2")
        self.assertEqual(spec["source"], self.sources["v2"]["source"])
        self.assertEqual(spec["master"], self.sources["v2"]["master"])
        self.assertEqual(spec["placement"], (164, 208))
        self.assertEqual(spec["canvas_bounds"], (169, 208, 224, 268))

        with tempfile.TemporaryDirectory(prefix="nh-cabir-preview-v2-test-") as temp:
            output_dir = Path(temp) / "overlay"
            preview.write_bundle(output_dir, version="v2")
            self.assertEqual(preview.verify_bundle(output_dir, version="v2"), output_dir.resolve())

            patch = json.loads((output_dir / preview.CREATURES_PATH).read_text(encoding="utf-8"))
            self.assertEqual(set(patch), {"core:gremlin", "core:masterGremlin"})
            self.assertTrue(
                all(
                    creature == {"graphics": {"animation": preview.ANIMATION_NAME}}
                    for creature in patch.values()
                )
            )

            descriptor = json.loads((output_dir / preview.ANIMATION_DESCRIPTOR).read_text(encoding="utf-8"))
            self.assertEqual(len(descriptor["sequences"]), 32)
            self.assertTrue(all(sequence["frames"] == ["00.png"] for sequence in descriptor["sequences"]))

            with Image.open(preview.source_spec("v2")["source"]) as opened:
                source = opened.convert("RGBA")
            expected = Image.new("RGBA", preview.LOGICAL_CANVAS, (0, 0, 0, 0))
            expected.alpha_composite(source, spec["placement"])
            with Image.open(output_dir / preview.NATIVE_IMAGE) as opened:
                actual = opened.convert("RGBA")
            self.assertEqual(actual.tobytes(), expected.tobytes())
            self.assertEqual(actual.getchannel("A").getbbox(), spec["canvas_bounds"])
            self.assertEqual((169 + 224) / 2, 196.5)
            self.assertEqual(spec["canvas_bounds"][3], 268)

            readme = (output_dir / "PREVIEW_README.md").read_text(encoding="utf-8")
            self.assertIn("unapproved rougher/darker v2 draft for static preview only", readme)
            self.assertIn("not approved for normal New Horizons bindings", readme)
            self.assertIn("the live/normal module stays untouched", readme)
            self.assertIn("Every one of the 32 declared creature animation groups", readme)
            self.assertIn("not a complete creature animation set", readme)

    def test_unknown_version_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown Cabir preview version"):
            preview.create_native_frame("v99")

    def test_pinned_source_and_master_changes_are_rejected(self):
        source = self.sources["v1"]["source"]
        source.write_bytes(source.read_bytes() + b"altered synthetic input")
        with self.assertRaisesRegex(ValueError, "standing preview hash changed"):
            preview.create_native_frame("v1")
        master = self.sources["v2"]["master"]
        master.write_bytes(master.read_bytes() + b"altered synthetic master")
        with self.assertRaisesRegex(ValueError, "master hash changed"):
            preview.create_native_frame("v2")

    def test_changed_alpha_geometry_is_rejected_even_with_matching_input_hash(self):
        spec = dict(self.sources["v1"])
        image = Image.new("RGBA", (64, 60), (0, 0, 0, 0))
        image.paste((180, 24, 40, 255), (5, 1, 61, 58))
        image.save(spec["source"])
        spec["source_sha256"] = preview._sha256(spec["source"].read_bytes())
        with patch.dict(preview.VERSION_SOURCES, {"v1": spec}):
            with self.assertRaisesRegex(ValueError, "alpha bounds changed"):
                preview.create_native_frame("v1")

    def test_generator_refuses_live_module_and_existing_output_paths(self):
        with self.assertRaises(ValueError):
            preview.write_bundle(ROOT / "Mods/new-horizons", version="v2")
        with self.assertRaises(ValueError):
            preview.write_bundle(ROOT / "assets/new-horizons/creatures/cabir/preview-copy")

        with tempfile.TemporaryDirectory(prefix="nh-cabir-preview-existing-") as temp:
            output_dir = Path(temp) / "existing"
            output_dir.mkdir()
            marker = output_dir / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                preview.write_bundle(output_dir)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")

    def test_paired_reference_bundle_preserves_frames_and_static_runtime_contract(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-reference-test-") as temp:
            temp_dir = Path(temp)
            base_input = temp_dir / "base.png"
            master_input = temp_dir / "master.png"
            base_bytes = self._write_reference_frame(base_input, (180, 24, 40, 255))
            master_bytes = self._write_reference_frame(master_input, (230, 170, 30, 255), (155, 80, 280, 350))
            base_sha = preview._sha256(base_bytes)
            master_sha = preview._sha256(master_bytes)

            output_dir = temp_dir / "overlay"
            self.assertEqual(preview.write_reference_bundle(output_dir, base_input, master_input), output_dir)
            self.assertEqual(preview.verify_reference_bundle(output_dir, base_input, master_input), output_dir)
            self.assertEqual(base_input.read_bytes(), base_bytes)
            self.assertEqual(master_input.read_bytes(), master_bytes)

            patch = json.loads((output_dir / preview.REFERENCE_PATCH).read_text(encoding="utf-8"))
            self.assertEqual(patch, preview.expected_reference_patch())
            self.assertNotEqual(
                patch["core:gremlin"]["graphics"]["animation"],
                patch["core:masterGremlin"]["graphics"]["animation"],
            )

            expected_groups = {group for group, _name in preview.ALL_CREATURE_GROUPS}
            for descriptor_path, animation_name in (
                (preview.REFERENCE_BASE_DESCRIPTOR, preview.REFERENCE_BASE_ANIMATION),
                (preview.REFERENCE_MASTER_DESCRIPTOR, preview.REFERENCE_MASTER_ANIMATION),
            ):
                descriptor = json.loads((output_dir / descriptor_path).read_text(encoding="utf-8"))
                self.assertEqual(descriptor, preview.expected_reference_animation_descriptor(animation_name))
                sequences = descriptor["sequences"]
                self.assertEqual({sequence["group"] for sequence in sequences}, expected_groups)
                for sequence in sequences:
                    self.assertEqual(sequence["generateOverlay"], 1)
                    expected_count = 3 if sequence["group"] in preview.SHOOTING_GROUPS else 2
                    self.assertEqual(len(sequence["frames"]), expected_count)
                    self.assertEqual(set(sequence["frames"]), {"frame-00.png"})

            self.assertEqual((output_dir / preview.REFERENCE_BASE_FRAME).read_bytes(), base_bytes)
            self.assertEqual((output_dir / preview.REFERENCE_MASTER_FRAME).read_bytes(), master_bytes)
            receipt = json.loads((output_dir / preview.REFERENCE_RECEIPT).read_text(encoding="utf-8"))
            self.assertEqual(receipt["visibility"], "private-only")
            self.assertIn("provenance", receipt["sourceProvenance"])
            self.assertEqual(receipt["sources"]["base"]["sha256"], base_sha)
            self.assertEqual(receipt["sources"]["master"]["sha256"], master_sha)
            self.assertEqual(receipt["runtimeFrames"]["base"]["sha256"], base_sha)
            self.assertEqual(receipt["runtimeFrames"]["master"]["sha256"], master_sha)
            readme = (output_dir / preview.REFERENCE_README).read_text(encoding="utf-8")
            self.assertIn("source provenance remains pending", readme.lower())
            self.assertIn("do not publish or commit", readme)
            self.assertIn("corpse pose remain static", readme)
            self.assertIn("repair action remains mechanically available", readme)

            (output_dir / preview.REFERENCE_MASTER_FRAME).write_bytes(base_bytes)
            with self.assertRaisesRegex(ValueError, "differs from its read-only input"):
                preview.verify_reference_bundle(output_dir, base_input, master_input)

    def test_paired_reference_validates_both_inputs_before_creating_output(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-reference-invalid-") as temp:
            temp_dir = Path(temp)
            valid = temp_dir / "valid.png"
            self._write_reference_frame(valid, (170, 20, 30, 255))
            invalid = temp_dir / "invalid.png"
            Image.new("RGBA", (450, 399), (0, 0, 0, 0)).save(invalid)
            output_dir = temp_dir / "must-not-exist"
            with self.assertRaisesRegex(ValueError, "450x400 RGBA"):
                preview.write_reference_bundle(output_dir, valid, invalid)
            self.assertFalse(output_dir.exists())

            opaque = temp_dir / "opaque.png"
            Image.new("RGBA", preview.REFERENCE_CANVAS, (1, 2, 3, 255)).save(opaque)
            with self.assertRaisesRegex(ValueError, "transparent canvas pixels"):
                preview.write_reference_bundle(output_dir, valid, opaque)
            self.assertFalse(output_dir.exists())

            empty = temp_dir / "empty.png"
            Image.new("RGBA", preview.REFERENCE_CANVAS, (0, 0, 0, 0)).save(empty)
            with self.assertRaisesRegex(ValueError, "visible pixels"):
                preview.write_reference_bundle(output_dir, valid, empty)
            self.assertFalse(output_dir.exists())

    def test_paired_reference_cli_requires_both_explicit_inputs(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-reference-cli-") as temp:
            temp_dir = Path(temp)
            valid = temp_dir / "valid.png"
            self._write_reference_frame(valid, (170, 20, 30, 255))
            output_dir = temp_dir / "overlay"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/export_new_horizons_cabir_ingame_preview.py"),
                    "--output-dir",
                    str(output_dir),
                    "--base-frame",
                    str(valid),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must be supplied together", result.stderr)
            self.assertFalse(output_dir.exists())

    def test_paired_reference_refuses_protected_existing_and_symlink_outputs(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-reference-paths-") as temp:
            temp_dir = Path(temp)
            base_input = temp_dir / "base.png"
            master_input = temp_dir / "master.png"
            self._write_reference_frame(base_input, (170, 20, 30, 255))
            self._write_reference_frame(master_input, (20, 120, 220, 255))

            with self.assertRaises(ValueError):
                preview.write_reference_bundle(ROOT / "Mods/new-horizons/reference-preview", base_input, master_input)
            with self.assertRaises(ValueError):
                preview.write_reference_bundle(
                    ROOT / "assets/new-horizons/creatures/cabir/reference-preview", base_input, master_input
                )

            existing = temp_dir / "existing"
            existing.mkdir()
            marker = existing / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                preview.write_reference_bundle(existing, base_input, master_input)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")

            target = temp_dir / "real-target"
            target.mkdir()
            link = temp_dir / "symlinked-output"
            try:
                link.symlink_to(target, target_is_directory=True)
            except OSError:
                self.skipTest("directory symlink creation is unavailable")
            with self.assertRaisesRegex(ValueError, "symlinked preview output"):
                preview.write_reference_bundle(link / "overlay", base_input, master_input)


if __name__ == "__main__":
    unittest.main()
