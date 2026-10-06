#!/usr/bin/env python3
"""Focused offline tests for generated Cabir v3 map-size resource bundles."""

import hashlib
import json
from pathlib import Path
import sys
import unittest

from PIL import Image, ImageChops, ImageOps


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import export_new_horizons_cabir_map_v3 as exporter  # noqa: E402


class CabirMapV3ExportTest(unittest.TestCase):
    def test_descriptors_have_one_four_frame_64px_map_walk_group(self):
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                spec = exporter.FAMILIES[family]
                output = ROOT / spec.output_relative
                descriptor = json.loads((output / spec.descriptor_name).read_text(encoding="utf-8"))
                self.assertEqual(set(descriptor), {"basepath", "sequences"})
                self.assertEqual(descriptor["basepath"], spec.descriptor_basepath)
                self.assertEqual(len(descriptor["sequences"]), 1)
                self.assertEqual(descriptor["sequences"][0]["group"], 0)
                self.assertEqual(descriptor["sequences"][0]["frames"], [
                    "walk/frame-00.png",
                    "walk/frame-01.png",
                    "walk/frame-02.png",
                    "walk/frame-03.png",
                ])
                self.assertEqual(output, ROOT / spec.output_relative)

    def test_map_walk_and_encounter_images_have_canvas_scale_and_alpha(self):
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                output = ROOT / exporter.FAMILIES[family].output_relative
                manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
                alignment = json.loads((output / "review/alignment.json").read_text(encoding="utf-8"))
                self.assertEqual(manifest["mapCanvas"], [64, 64])
                self.assertEqual(manifest["mapGroup"], 0)
                self.assertEqual(manifest["mapWalkFrames"], 4)
                self.assertEqual(manifest["targetVisibleBodyHeight"], 42.0)
                self.assertEqual(manifest["targetRootPivot"], [32.0, 60.0])
                self.assertAlmostEqual(alignment["walkPoses"][0]["uniformScaleForSourceSheet"], alignment["walkPoses"][3]["uniformScaleForSourceSheet"])
                self.assertEqual(len(alignment["walkPoses"]), 4)
                for frame in alignment["walkPoses"]:
                    self.assertIn(frame["outputVisibleBodyHeight"], range(40, 45))
                    self.assertEqual(frame["outputCanvas"], [64, 64])
                    self.assertEqual(frame["outputAlphaThresholdBBox"][3], 60 if frame["outputVisibleBodyHeight"] == 42 else 61)

                for name in (
                    f"{manifest['descriptorBasepath']}walk/frame-00.png",
                    f"{manifest['descriptorBasepath']}walk/frame-01.png",
                    f"{manifest['descriptorBasepath']}walk/frame-02.png",
                    f"{manifest['descriptorBasepath']}walk/frame-03.png",
                    manifest["encounterResources"]["left"],
                    manifest["encounterResources"]["right"],
                ):
                    with Image.open(output / name) as image:
                        self.assertEqual(image.format, "PNG")
                        self.assertEqual(image.mode, "RGBA")
                        self.assertEqual(image.size, (64, 64))
                        self.assertIsNotNone(image.getchannel("A").getbbox())

    def test_encounter_opposite_facing_is_only_a_horizontal_mirror(self):
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                output = ROOT / exporter.FAMILIES[family].output_relative
                manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
                left_name = manifest["encounterResources"]["left"]
                right_name = manifest["encounterResources"]["right"]
                with Image.open(output / left_name) as left_source, Image.open(output / right_name) as right_source:
                    left = left_source.copy()
                    right = right_source.copy()
                self.assertEqual(ImageChops.difference(left, ImageOps.mirror(right)).getbbox(), None)
                alignment = json.loads((output / "review/alignment.json").read_text(encoding="utf-8"))
                self.assertIn("mechanical horizontal mirror", alignment["encounterPoses"][left_name]["orientation"])

    def test_manifest_pins_authored_sources_but_contains_no_original_pixels(self):
        expected = {
            "cabir": {
                "standing": "e9e5cfacb4cafa8f55c0160a400a4f0c6ea3426d77f631c928de27eaa4033060",
                "walk": "112c20fb49091bcbf5efc5646b7ce1950f37ade03969a12c3ccedcffdecace2a",
            },
            "cabir-master": {
                "standing": "c1c88872bf0cc8e969f9d883b289aff86071db31241d35a16255b74532fbca15",
                "walk": "66c9402eaa19e17a50840a0871150b2ed9db72062dad0511ba7b37c1e272d537",
            },
        }
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                output = ROOT / exporter.FAMILIES[family].output_relative
                manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
                self.assertEqual(manifest["sourcePins"]["standing"]["sha256"], expected[family]["standing"])
                self.assertEqual(manifest["sourcePins"]["walk"]["sha256"], expected[family]["walk"])
                self.assertFalse(manifest["sourceFilesModified"])
                self.assertFalse(manifest["gameplayOrRuntimeBindingIncluded"])
                self.assertIn("no purchaser game pixel data", manifest["referenceOnlyOriginalResourceMetadata"]["provenanceNote"])
                for path, digest in manifest["files"].items():
                    file_path = output / path
                    self.assertEqual(hashlib.sha256(file_path.read_bytes()).hexdigest(), digest)
                    self.assertTrue(file_path.is_relative_to(output))
                self.assertEqual(
                    set(manifest["files"]),
                    {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file() and path.name != "manifest.json"},
                )
                self.assertEqual(manifest["referenceOnlyOriginalResourceMetadata"]["mapAnimation"], {
                    "resource": "AVWgrem0/AVWgrex0", "group": 0, "frameCount": 8, "canvas": [64, 64],
                })
                self.assertEqual(manifest["referenceOnlyOriginalResourceMetadata"]["encounter"]["frameIndices"], {
                    "right": 56 if family == "cabir" else 58,
                    "left": 57 if family == "cabir" else 59,
                })

    def test_contacts_keep_the_sprite_tiny_at_native_scale_and_expand_nearest(self):
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                output = ROOT / exporter.FAMILIES[family].output_relative
                with Image.open(output / "review/map-walk-contact-1x.png") as native:
                    self.assertEqual(native.size, (128, 156))
                with Image.open(output / "review/map-walk-contact-4x-nearest.png") as nearest:
                    self.assertEqual(nearest.size, (512, 568))
                with Image.open(output / "review/encounter-facing-contact-1x.png") as native:
                    self.assertEqual(native.size, (128, 78))
                with Image.open(output / "review/encounter-facing-contact-4x-nearest.png") as nearest:
                    self.assertEqual(nearest.size, (512, 284))

    def test_installed_unique_and_legacy_map_resources_match_pinned_exports(self):
        runtime_root = ROOT / "Mods/new-horizons"
        expected_descriptors = [
            runtime_root / "Content/sprites/NH_CabirMap.json",
            runtime_root / "Content/sprites/NH_CabirMasterMap.json",
            runtime_root / "Content/sprites/AVWgrem0.json",
            runtime_root / "Content/sprites/AVWgrex0.json",
        ]
        self.assertTrue(all(path.is_file() for path in expected_descriptors), "map descriptor installation is incomplete")
        for family, unique_name, legacy_name in (
            ("cabir", "NH_CabirMap.json", "AVWgrem0.json"),
            ("cabir-master", "NH_CabirMasterMap.json", "AVWgrex0.json"),
        ):
            output = ROOT / exporter.FAMILIES[family].output_relative
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            unique = json.loads((output / manifest["descriptor"]).read_text(encoding="utf-8"))
            unique_path = runtime_root / "Content/sprites" / unique_name
            unique_installed = json.loads(unique_path.read_text(encoding="utf-8"))
            self.assertEqual(unique_installed, unique)

            legacy_path = runtime_root / "Content/sprites" / legacy_name
            legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
            self.assertEqual(legacy["basepath"], unique["basepath"])
            self.assertEqual(len(legacy["sequences"]), 1)
            self.assertEqual(legacy["sequences"][0]["group"], 0)
            self.assertEqual(legacy["sequences"][0]["frames"], unique["sequences"][0]["frames"] * 2)

            runtime_images = runtime_root / "Images"
            for relative in unique["sequences"][0]["frames"]:
                source = output / unique["basepath"] / relative
                installed = runtime_images / unique["basepath"] / relative
                self.assertEqual(hashlib.sha256(installed.read_bytes()).hexdigest(), hashlib.sha256(source.read_bytes()).hexdigest())

            for key in ("left", "right"):
                resource_name = manifest["encounterResources"][key]
                source = output / resource_name
                installed = runtime_images / resource_name
                self.assertEqual(hashlib.sha256(installed.read_bytes()).hexdigest(), hashlib.sha256(source.read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main(verbosity=2)
