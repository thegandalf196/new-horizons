"""Focused integrity tests for the private Magi handoff importer."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import import_new_horizons_magi_handoff as handoff  # noqa: E402


class MagiHandoffImporterTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="nh-magi-handoff-test-", dir=ROOT / "build")
        self.handoff_root = Path(self.temp.name) / "handoff"
        self.package = self.handoff_root / handoff.PACKAGE_RELATIVE.as_posix()
        self.sprite_root = self.package / handoff.SPRITE_SUBTREE.as_posix()
        self.package.mkdir(parents=True)
        self.png_bytes: dict[str, bytes] = {}
        self._make_package()

    def tearDown(self):
        self.temp.cleanup()

    def _write_package(self, relative: str, data: bytes) -> None:
        path = self.package / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def _write_png(self, relative: str) -> None:
        data = b"private-test-png:" + relative.encode("utf-8")
        self._write_package(f"Content/sprites/{relative}", data)
        self.png_bytes[relative] = data

    def _write_descriptor(self, relative: str, basepath: str, sequences: list[dict]) -> None:
        self._write_package(
            f"Content/sprites/{relative}",
            (json.dumps({"basepath": basepath, "sequences": sequences}, indent=2) + "\n").encode("utf-8"),
        )

    def _make_package(self) -> None:
        for form, descriptor in (
            ("magi", "magi-vcmi-complete/magi/NH_CMAGE.json"),
            ("archmagi", "magi-vcmi-complete/archmagi/NH_CAMAGE.json"),
        ):
            basepath = f"magi-vcmi-complete/battle/{form}/"
            sequences = []
            for group, (action, count) in handoff.BATTLE_GROUPS.items():
                frames = [f"{action}/{index:03d}.png" for index in range(count)]
                for frame in frames:
                    self._write_png(basepath + frame)
                sequences.append({"group": group, "frames": frames, "generateShadow": 0})
            self._write_descriptor(descriptor, basepath, sequences)

        for form, descriptor, basename in (
            ("magi", "AVWmage0.json", "map/magi"),
            ("archmagi", "AVWmagx0.json", "map/archmagi"),
        ):
            basepath = f"magi-vcmi-complete/{basename}/"
            frames = [f"{index:03d}.png" for index in range(30)]
            for frame in frames:
                self._write_png(basepath + frame)
            self._write_descriptor(descriptor, basepath, [{"group": 0, "frames": frames, "generateShadow": 0}])

        projectile_base = "magi-vcmi-complete/projectile/"
        projectile_frames = [f"red-magi-angle-{index}.png" for index in range(9)]
        for frame in projectile_frames:
            self._write_png(projectile_base + frame)
        self._write_descriptor(
            "magi-vcmi-complete/projectile/NH_PMAGEX.json",
            projectile_base,
            [{"group": 0, "frames": projectile_frames, "generateShadow": 0}],
        )
        for icon in handoff.ICON_FILES:
            self._write_png(icon)

        ray = {
            "attackClimaxFrame": 8,
            "ray": [
                {"start": [192, 32, 32, 255], "end": [192, 32, 32, 64]},
                {"start": [224, 128, 128, 255], "end": [224, 128, 128, 128]},
                {"start": [176, 32, 32, 255], "end": [176, 32, 32, 255]},
                {"start": [224, 128, 128, 255], "end": [224, 128, 128, 128]},
                {"start": [192, 32, 32, 255], "end": [192, 32, 32, 64]},
            ],
        }
        self._write_package(
            "Content/config/creatures/tower.json",
            (json.dumps({
                "core:mage": {"graphics": {
                    "animation": "magi-vcmi-complete/magi/NH_CMAGE.def",
                    "iconSmall": "magi-vcmi-complete/icons/magi-small-32.png",
                    "iconLarge": "magi-vcmi-complete/icons/magi-portrait-58x64.png",
                    "mapAttackFromRight": "magi-vcmi-complete/icons/magi-map-attack-68.png",
                    "mapAttackFromLeft": "magi-vcmi-complete/icons/magi-map-attack-69.png",
                    "missile": {"projectile": "magi-vcmi-complete/projectile/NH_PMAGEX.def"},
                }},
                "core:archMage": {"graphics": {
                    "animation": "magi-vcmi-complete/archmagi/NH_CAMAGE.def",
                    "iconSmall": "magi-vcmi-complete/icons/archmagi-small-32.png",
                    "iconLarge": "magi-vcmi-complete/icons/archmagi-portrait-58x64.png",
                    "mapAttackFromRight": "magi-vcmi-complete/icons/archmagi-map-attack-70.png",
                    "mapAttackFromLeft": "magi-vcmi-complete/icons/archmagi-map-attack-71.png",
                    "missile": ray,
                }},
            }, indent=2) + "\n").encode("utf-8"),
        )
        self._write_package(
            "mod.json",
            json.dumps({"modType": "Graphical", "depends": ["new-horizons"],
                        "creatures": ["config/creatures/tower.json"]}).encode("utf-8"),
        )
        self._write_package(
            "resource-bindings.json",
            json.dumps({"nativeMapOverlay": {
                "magi": {"resource": "AVWmage0.json"},
                "archmagi": {"resource": "AVWmagx0.json"},
            }}).encode("utf-8"),
        )

        runtime_ray = self.handoff_root / "inputs/runtime-support/archmagi-red-ray.json"
        runtime_ray.parent.mkdir(parents=True)
        runtime_ray.write_text(
            json.dumps({"archMage": {"graphics": {"missile": ray}}}),
            encoding="utf-8",
        )
        pins = {}
        for path in self.package.rglob("*"):
            if path.is_file():
                relative = path.relative_to(self.package).as_posix()
                key = (handoff.PACKAGE_RELATIVE / Path(relative)).as_posix()
                pins[key] = hashlib.sha256(path.read_bytes()).hexdigest()
        pins["inputs/runtime-support/archmagi-red-ray.json"] = hashlib.sha256(runtime_ray.read_bytes()).hexdigest()
        (self.handoff_root / "SHA256.json").write_text(json.dumps(pins), encoding="utf-8")

    def test_private_mount_copies_all_pngs_and_adapts_only_descriptors(self):
        outputs, stats = handoff.build_overlay(self.handoff_root, expected_manifest_sha256=None)

        self.assertEqual(stats["pngCount"], 345)
        self.assertEqual(stats["descriptorCount"], 5)
        self.assertEqual(stats["magiBattleFrames"], 133)
        self.assertEqual(stats["archmagiBattleFrames"], 133)
        self.assertEqual(stats["mapFrames"], 60)
        self.assertEqual(stats["projectileAngles"], 9)
        self.assertEqual(stats["copiedFileCount"], 350)
        self.assertEqual(len(outputs), 351)

        for source_relative, data in self.png_bytes.items():
            mounted = f"Mods/new-horizons/Images/{source_relative}"
            self.assertEqual(outputs[mounted], data)
        self.assertNotIn("Mods/new-horizons/Content/config/creatures/tower.json", outputs)
        self.assertNotIn("Mods/new-horizons/mod.json", outputs)
        self.assertNotIn("mod.json", outputs)

        for relative in (
            "magi-vcmi-complete/magi/NH_CMAGE.json",
            "magi-vcmi-complete/archmagi/NH_CAMAGE.json",
        ):
            descriptor = json.loads(outputs[f"Mods/new-horizons/Content/sprites/{relative}"])
            self.assertEqual(descriptor["basepath"], relative.split("/")[0] + "/battle/" + relative.split("/")[1] + "/")
            self.assertTrue(all(sequence["generateShadow"] == 0 for sequence in descriptor["sequences"]))
            self.assertTrue(all(sequence["generateOverlay"] == 1 for sequence in descriptor["sequences"]))

        for relative in ("AVWmage0.json", "AVWmagx0.json", "magi-vcmi-complete/projectile/NH_PMAGEX.json"):
            descriptor = json.loads(outputs[f"Mods/new-horizons/Content/sprites/{relative}"])
            self.assertTrue(all(sequence["generateShadow"] == 0 for sequence in descriptor["sequences"]))
            self.assertTrue(all("generateOverlay" not in sequence for sequence in descriptor["sequences"]))

        patch = json.loads(outputs["GRAPHICS_PATCH.json"])
        self.assertEqual(set(patch), {"creatures"})
        self.assertEqual(patch["creatures"]["core:mage"]["remove"], [])
        self.assertEqual(
            patch["creatures"]["core:archMage"]["remove"],
            ["map", "mapMask", "mapAttackFromLeft", "mapAttackFromRight"],
        )
        arch_graphics = patch["creatures"]["core:archMage"]["set"]
        self.assertEqual(arch_graphics["missile"]["attackClimaxFrame"], 8)
        self.assertEqual(len(arch_graphics["missile"]["ray"]), 5)
        self.assertNotIn("animationTime", arch_graphics)
        self.assertNotIn("timeBetweenFidgets", arch_graphics)
        self.assertNotIn("frameAngles", arch_graphics["missile"])
        self.assertNotIn("offset", arch_graphics["missile"])
        self.assertNotIn("map", arch_graphics)
        self.assertNotIn("mapMask", arch_graphics)

    def test_write_and_check_are_reproducible_and_refuse_overwrite(self):
        output = Path(self.temp.name) / "candidate"
        built = handoff.write_overlay(self.handoff_root, output, expected_manifest_sha256=None)
        checked = handoff.write_overlay(self.handoff_root, output, check=True, expected_manifest_sha256=None)
        self.assertEqual(built, checked)
        with self.assertRaisesRegex(handoff.ImportValidationError, "refusing to overwrite"):
            handoff.write_overlay(self.handoff_root, output, expected_manifest_sha256=None)

        (output / "extra.txt").write_text("unexpected", encoding="utf-8")
        with self.assertRaisesRegex(handoff.ImportValidationError, "output set differs"):
            handoff.write_overlay(self.handoff_root, output, check=True, expected_manifest_sha256=None)

    def test_source_hash_mismatch_and_unsafe_descriptor_paths_are_rejected(self):
        changed_png = self.sprite_root / "magi-vcmi-complete/icons/magi-small-32.png"
        changed_png.write_bytes(b"changed after manifest")
        with self.assertRaisesRegex(handoff.ImportValidationError, "hash mismatch"):
            handoff.build_overlay(self.handoff_root, expected_manifest_sha256=None)

        changed_png.write_bytes(self.png_bytes["magi-vcmi-complete/icons/magi-small-32.png"])
        descriptor_path = self.sprite_root / "magi-vcmi-complete/magi/NH_CMAGE.json"
        descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
        descriptor["basepath"] = "../outside/"
        descriptor_bytes = (json.dumps(descriptor, indent=2) + "\n").encode("utf-8")
        descriptor_path.write_bytes(descriptor_bytes)
        checksum_path = self.handoff_root / "SHA256.json"
        pins = json.loads(checksum_path.read_text(encoding="utf-8"))
        pins["package/magi-vcmi-complete/Content/sprites/magi-vcmi-complete/magi/NH_CMAGE.json"] = hashlib.sha256(descriptor_bytes).hexdigest()
        checksum_path.write_text(json.dumps(pins), encoding="utf-8")
        with self.assertRaisesRegex(handoff.ImportValidationError, "unsafe sprite basepath"):
            handoff.build_overlay(self.handoff_root, expected_manifest_sha256=None)

    def test_output_must_remain_in_private_build_tree(self):
        with self.assertRaisesRegex(handoff.ImportValidationError, "must stay below repository build"):
            handoff._safe_output(ROOT / "Mods" / "unsafe-magi-import")


if __name__ == "__main__":
    unittest.main()
