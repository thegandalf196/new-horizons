#!/usr/bin/env python3
"""Focused offline tests for generated Cabir v3 map-size resource bundles."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
from dataclasses import replace
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from PIL import Image, ImageChops, ImageOps


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import export_new_horizons_cabir_map_v3 as exporter  # noqa: E402


class CabirMapV3ExportTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="nh-cabir-map-synthetic-")
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name)
        self.source_pins = {}
        configs = {}
        for family, original in exporter.battle_export.FAMILIES.items():
            config = dict(original)
            pins = {}
            for role in ("standing", "walk", "prompt"):
                relative = f"assets/new-horizons/creatures/{family}/synthetic-{role}"
                path = self.workspace / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if role == "prompt":
                    path.write_text("Synthetic map-export algorithm fixture; no artwork claim.\n")
                else:
                    image = Image.new("RGBA", (60, 100), (0, 0, 0, 0))
                    image.paste((180, 24, 40, 255), (10, 0, 45, 100))
                    image.save(path, format="PNG")
                pins[role] = exporter.battle_export.PinnedFile(relative, exporter._sha256(path))
            config["standing"] = pins["standing"]
            config["standing_prompt"] = pins["prompt"]
            config["atlases"] = dict(config["atlases"])
            config["atlases"]["walk"] = replace(config["atlases"]["walk"], source=pins["walk"],
                prompt=pins["prompt"], cleanup_receipt=None)
            configs[family] = config
            self.source_pins[family] = pins

        def standing(pinned, prompt, family):
            with Image.open(self.workspace / pinned.path) as image:
                pose = exporter.preview.NativePose("standing", image.copy(), pinned.path,
                    pinned.sha256, (0, 0), {"kind": "synthetic standing input"})
            return pose, {"sourceSha256": pinned.sha256, "synthetic": True}

        def atlas(spec):
            return SimpleNamespace(spec=spec, receipt={"synthetic": True})

        def poses(atlas):
            result = []
            for index in range(4):
                image = Image.new("RGBA", (60, 100), (0, 0, 0, 0))
                image.paste((180, 24 + index * 20, 40, 255), (10, 0, 45, 100))
                result.append(exporter.preview.NativePose(f"walk-{index}", image,
                    atlas.spec.source.path, atlas.spec.source.sha256, (index * 60, 0),
                    {"kind": "synthetic separated walk input"}))
            return result

        for replacement in (patch.dict(exporter.battle_export.FAMILIES, configs),
                            patch.object(exporter.battle_export, "_extract_standing", side_effect=standing),
                            patch.object(exporter.battle_export, "_extract_atlas", side_effect=atlas),
                            patch.object(exporter.battle_export, "_atlas_poses", side_effect=poses)):
            replacement.start()
            self.addCleanup(replacement.stop)
        for family in exporter.FAMILIES:
            output, artifacts = exporter.build_family(family, self.workspace)
            exporter._write_artifacts(output, artifacts)
            exporter._check_artifacts(output, artifacts)

    def test_descriptors_have_one_four_frame_64px_map_walk_group(self):
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                spec = exporter.FAMILIES[family]
                output = self.workspace / spec.output_relative
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
                self.assertEqual(output, self.workspace / spec.output_relative)

    def test_map_walk_and_encounter_images_have_canvas_scale_and_alpha(self):
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                output = self.workspace / exporter.FAMILIES[family].output_relative
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
                output = self.workspace / exporter.FAMILIES[family].output_relative
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
        for family in ("cabir", "cabir-master"):
            with self.subTest(family=family):
                output = self.workspace / exporter.FAMILIES[family].output_relative
                manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
                self.assertEqual(manifest["sourcePins"]["standing"]["sha256"], self.source_pins[family]["standing"].sha256)
                self.assertEqual(manifest["sourcePins"]["walk"]["sha256"], self.source_pins[family]["walk"].sha256)
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
                output = self.workspace / exporter.FAMILIES[family].output_relative
                with Image.open(output / "review/map-walk-contact-1x.png") as native:
                    self.assertEqual(native.size, (128, 156))
                with Image.open(output / "review/map-walk-contact-4x-nearest.png") as nearest:
                    self.assertEqual(nearest.size, (512, 568))
                with Image.open(output / "review/encounter-facing-contact-1x.png") as native:
                    self.assertEqual(native.size, (128, 78))
                with Image.open(output / "review/encounter-facing-contact-4x-nearest.png") as nearest:
                    self.assertEqual(nearest.size, (512, 284))

    def test_current_shipping_map_and_encounter_bindings_resolve_verified_nhart_frames(self):
        if __package__:
            from .nhart_test_resources import ArtPath
            from .test_new_horizons_content import load
        else:
            from nhart_test_resources import ArtPath
            from test_new_horizons_content import load
        sprites = ArtPath()
        creatures = load("Mods/new-horizons/Content/config/creatures/tower.json")
        for creature, native, encounter in (
            ("core:gremlin", "NH_CabirHandoffNativeMap", "NH_CabirHandoffMap"),
            ("core:masterGremlin", "NH_CabirMasterHandoffNativeMap", "NH_CabirMasterHandoffMap"),
        ):
            graphics = creatures[creature]["graphics"]
            self.assertEqual(graphics["map"], native + ".def")
            self.assertEqual(graphics["mapAttackFromLeft"], encounter + ":0:0")
            self.assertEqual(graphics["mapAttackFromRight"], encounter + ":0:0")
            for name, size in ((native, (64, 64)), (encounter, (128, 112))):
                descriptor = json.loads((sprites / (name + ".json")).read_text())
                self.assertEqual(len(descriptor["sequences"]), 1)
                sequence = descriptor["sequences"][0]
                self.assertEqual(sequence["group"], 0)
                self.assertEqual(sequence["frames"], [f"walk/frame-{i:02d}.png" for i in range(7)])
                for frame in sequence["frames"]:
                    with (sprites / descriptor["basepath"] / frame).open_image() as image:
                        self.assertEqual(image.size, size)
                        self.assertEqual(image.mode, "RGBA")
                        self.assertIsNotNone(image.getchannel("A").getbbox())

    def test_explicit_external_workspace_required_and_existing_outputs_not_replaced(self):
        with self.assertRaisesRegex(ValueError, "explicit external"):
            exporter.build_family("cabir")
        with self.assertRaisesRegex(ValueError, "outside the checkout"):
            exporter.build_family("cabir", ROOT / "assets/new-horizons")
        with self.assertRaisesRegex(ValueError, "outside the checkout"):
            exporter._write_artifacts(ROOT / "assets/new-horizons/map-test", {})
        output, artifacts = exporter.build_family("cabir", self.workspace)
        with self.assertRaises(FileExistsError):
            exporter._write_artifacts(output, artifacts)
        exporter._check_artifacts(output, artifacts)

    def test_output_tampering_rejected_without_altering_synthetic_sources(self):
        output, artifacts = exporter.build_family("cabir", self.workspace)
        frame = output / exporter.FAMILIES["cabir"].descriptor_basepath / "walk/frame-00.png"
        frame.write_bytes(frame.read_bytes() + b"synthetic tamper")
        with self.assertRaisesRegex(ValueError, "artifact differs"):
            exporter._check_artifacts(output, artifacts)
        for pins in self.source_pins.values():
            for pin in pins.values():
                self.assertEqual(exporter._sha256(self.workspace / pin.path), pin.sha256)


if __name__ == "__main__":
    unittest.main(verbosity=2)
