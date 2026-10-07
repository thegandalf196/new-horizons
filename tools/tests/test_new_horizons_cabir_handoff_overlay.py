"""Focused integrity tests for the private Cabir handoff overlay exporter."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import import_new_horizons_cabir_handoff as handoff  # noqa: E402


class CabirHandoffOverlayTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="nh-cabir-handoff-test-", dir=ROOT / "build")
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        self.package.mkdir()
        self._make_package()

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def _save_rgba(path: Path, size: tuple[int, int], color: tuple[int, int, int, int], *, walk=False):
        path.parent.mkdir(parents=True, exist_ok=True)
        image = Image.new("RGBA", size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        if walk:
            draw.rectangle((45, 28, 78, 84), fill=color)
            draw.rectangle((60, 85, 64, 85), fill=color)
        else:
            draw.rectangle((20, 20, min(30, size[0] - 1), min(30, size[1] - 1)), fill=color)
        image.save(path, format="PNG", optimize=False)

    def _make_package(self):
        source_paths = []
        for form_index, form in enumerate(("cabir", "cabir-master")):
            for index in range(7):
                relative = Path(f"animations/{form}/walk/frame-{index:02}.png")
                self._save_rgba(self.package / relative, (128, 112), (180 + form_index, 40, index + 1, 255), walk=True)
                source_paths.append(relative)
            for action in ("melee", "ranged"):
                for index in range(6):
                    relative = Path(f"animations/{form}/{action}/frame-{index:02}.png")
                    self._save_rgba(self.package / relative, (450, 400), (20 + index, 80 + form_index, 100, 255))
                    source_paths.append(relative)

        icon_names = (
            "cabir-large-58x64.png",
            "cabir-small-32x32.png",
            "cabir-portrait-cutout-58x64.png",
            "cabir-master-large-58x64.png",
            "cabir-master-small-32x32.png",
            "cabir-master-portrait-cutout-58x64.png",
        )
        for index, name in enumerate(icon_names):
            size = (32, 32) if "small" in name else (58, 64)
            relative = Path("icons") / name
            self._save_rgba(self.package / relative, size, (index, 90, 140, 255))
            source_paths.append(relative)

        for index in range(9):
            relative = Path(f"projectile/cabir-fire-angle-{index:02}.png")
            self._save_rgba(self.package / relative, (50, 50), (200, index, 30, 255))
            source_paths.append(relative)

        pins = {
            path.as_posix(): hashlib.sha256((self.package / path).read_bytes()).hexdigest()
            for path in source_paths
        }
        handoff_manifest = {
            "approval": "User approved Cabir forms/ember projectile 2026-10-07",
            "fileCount": 53,
            "coverage": {"nativeFrames": 38, "icons": 6, "projectileAngles": 9},
            "sha256": pins,
        }
        (self.package / "HANDOFF_MANIFEST.json").write_text(json.dumps(handoff_manifest), encoding="utf-8")
        integration_map = {
            "animations": {
                "walk": {"group": 0, "countPerForm": 7, "canvas": [128, 112], "feetBaselineY": 86},
                "melee": {"group": 12, "countPerForm": 6, "canvas": [450, 400], "feetBaselineY": 268, "rootX": 196.5},
                "ranged": {
                    "group": 15,
                    "countPerForm": 6,
                    "canvas": [450, 400],
                    "feetBaselineY": 268,
                    "rootX": 196.5,
                    "releasePoseIndex": 3,
                    "mouthCanvasXY": [230, 223],
                },
            },
            "icons": {"small": [32, 32], "large": [58, 64], "portraitCutout": [58, 64]},
            "projectile": {"frames": 9, "canvas": [50, 50], "horizontalAngleFrame": 4},
        }
        (self.package / "integration-map.json").write_text(json.dumps(integration_map), encoding="utf-8")

    def test_only_supported_partial_groups_are_emitted(self):
        outputs, metadata = handoff.build_overlay(self.package, expected_manifest_sha256=None)
        for form_key, form in handoff.FORMS.items():
            descriptor_path = f"overlay/Mods/new-horizons/Content/sprites/{form['battleDescriptor']}"
            descriptor = json.loads(outputs[descriptor_path])
            self.assertEqual([group["group"] for group in descriptor["sequences"]], [0, 2, 12, 15])
            self.assertEqual([len(group["frames"]) for group in descriptor["sequences"]], [7, 1, 6, 6])
            self.assertEqual(descriptor["sequences"][1]["frames"], ["holding/frame-00.png"])
            for sequence in descriptor["sequences"]:
                for frame in sequence["frames"]:
                    referenced_image = f"overlay/Mods/new-horizons/Images/{descriptor['basepath']}{frame}"
                    self.assertIn(referenced_image, outputs)
            holding_path = (
                f"overlay/Mods/new-horizons/Images/cabir-handoff/{form_key}/battle/holding/frame-00.png"
            )
            melee_ready_path = (
                f"overlay/Mods/new-horizons/Images/cabir-handoff/{form_key}/battle/melee/frame-00.png"
            )
            self.assertEqual(outputs[holding_path], outputs[melee_ready_path])

            map_path = f"overlay/Mods/new-horizons/Content/sprites/{form['mapDescriptor']}"
            map_descriptor = json.loads(outputs[map_path])
            self.assertEqual(map_descriptor["sequences"][0]["group"], 0)
            self.assertEqual(len(map_descriptor["sequences"][0]["frames"]), 7)
            for frame in map_descriptor["sequences"][0]["frames"]:
                referenced_image = f"overlay/Mods/new-horizons/Images/{map_descriptor['basepath']}{frame}"
                self.assertIn(referenced_image, outputs)

        self.assertIn("31", metadata["missingBattleGroups"])
        self.assertIn("7", metadata["missingBattleGroups"])
        self.assertNotIn("0", metadata["missingBattleGroups"])
        self.assertFalse(metadata["installationReady"])
        self.assertFalse(metadata["gameplayChanged"])

    def test_walk_translation_is_one_integer_offset_without_resampling(self):
        outputs, metadata = handoff.build_overlay(self.package, expected_manifest_sha256=None)
        transform = metadata["walkTransform"]["cabir"]
        self.assertEqual(transform["offsetXY"], [134, 182])
        self.assertEqual(transform["pixelResampling"], "none")
        for index in range(7):
            source = Image.open(self.package / f"animations/cabir/walk/frame-{index:02}.png").convert("RGBA")
            battle_relative = (
                f"overlay/Mods/new-horizons/Images/cabir-handoff/cabir/battle/walk/frame-{index:02}.png"
            )
            map_relative = (
                f"overlay/Mods/new-horizons/Images/cabir-handoff/cabir/map/walk/frame-{index:02}.png"
            )
            target = Image.open(__import__("io").BytesIO(outputs[battle_relative])).convert("RGBA")
            self.assertEqual(target.size, (450, 400))
            self.assertEqual(target.crop((134, 182, 262, 294)).tobytes(), source.tobytes())
            self.assertEqual(outputs[map_relative], (self.package / f"animations/cabir/walk/frame-{index:02}.png").read_bytes())

    def test_projectile_and_release_metadata_are_explicit(self):
        outputs, metadata = handoff.build_overlay(self.package, expected_manifest_sha256=None)
        descriptor_path = f"overlay/Mods/new-horizons/Content/sprites/{handoff.PROJECTILE_DESCRIPTOR}"
        descriptor = json.loads(outputs[descriptor_path])
        self.assertEqual(len(descriptor["images"]), 9)
        self.assertEqual([(item["group"], item["frame"]) for item in descriptor["images"]], [(0, i) for i in range(9)])
        for image in descriptor["images"]:
            referenced_image = f"overlay/Mods/new-horizons/Images/{descriptor['basepath']}{image['file']}"
            self.assertIn(referenced_image, outputs)
        release = metadata["rangedRelease"]
        self.assertEqual(release["engineAttackClimaxFrameOneBased"], 4)
        self.assertEqual(release["mouthOnBattleCanvas"], [230, 223])
        self.assertEqual(release["frontMiddleProjectileOffset"], [33, -42])
        self.assertEqual(release["horizontalAngleFrameZeroBased"], 4)
        self.assertIn("not supplied", release["upperAndLowerProjectileOrigins"])

    def test_output_is_reproducible_and_refuses_overwrite_or_unpinned_inputs(self):
        destination = self.root / "candidate"
        built = handoff.write_overlay(self.package, destination, expected_manifest_sha256=None)
        checked = handoff.write_overlay(self.package, destination, check=True, expected_manifest_sha256=None)
        self.assertEqual(built["outputHashes"], checked["outputHashes"])
        with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
            handoff.write_overlay(self.package, destination, expected_manifest_sha256=None)

        changed = self.package / "projectile/cabir-fire-angle-00.png"
        changed.write_bytes(changed.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            handoff.build_overlay(self.package, expected_manifest_sha256=None)

    def test_output_must_stay_below_build_and_must_not_be_symlink(self):
        with self.assertRaisesRegex(ValueError, "below the repository build"):
            handoff._safe_output(ROOT / "output" / "unsafe-candidate")
        target = self.root / "target"
        target.mkdir()
        link = self.root / "link"
        link.symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            handoff._safe_output(link)

    def test_reviewed_manifest_identity_is_pinned_by_default(self):
        with self.assertRaisesRegex(ValueError, "manifest identity changed"):
            handoff.build_overlay(self.package, expected_manifest_sha256=handoff.HANDOFF_MANIFEST_SHA256)


if __name__ == "__main__":
    unittest.main()
