#!/usr/bin/env python3
"""Focused pin, registration, and safe-install checks for Academy map v3."""

import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from PIL import Image
from tools.tests.nhart_test_resources import ArtPath
from tools.tests.test_new_horizons_academy_art import shipping_revision, private_revision_fixture


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ArtPath()
ART_PATCH = ROOT / "Mods/new-horizons/Content/config/factions/academyArt.json"
sys.path.insert(0, str(ROOT / "tools"))
import import_new_horizons_academy_assets as academy_importer


class NewHorizonsAcademyMapV3Test(unittest.TestCase):
    def test_v3_pin_and_runtime_preserve_v2_registration_and_engine_layers(self):
        v2 = {"manifest": json.loads((ROOT / academy_importer.MAP_REVISION_MANIFEST).read_text())}
        v3 = shipping_revision("map", "v3")
        self.assertEqual(v2["manifest"]["revision"], "v2")
        self.assertEqual(v3["manifest"]["revision"], "v3")
        self.assertEqual(v3["manifest_sha256"], academy_importer.APPROVED_MAP_REVISION_V3_MANIFEST_SHA256)

        invariant_fields = (
            "sourceMaster",
            "resource",
            "runtime",
            "masterSize",
            "masterSolidBox",
            "sourceSolidBox",
            "dimensions",
            "resampling",
        )
        for name, expected in academy_importer.MAP_REVISION_SLOTS.items():
            with self.subTest(body=name):
                old_record = v2["manifest"]["bodies"][name]
                new_record = v3["manifest"]["bodies"][name]
                for field in invariant_fields:
                    self.assertEqual(new_record[field], old_record[field], field)
                runtime = IMAGES / expected["runtime"]
                self.assertEqual(runtime.read_bytes(), v3["exports_by_runtime"][expected["runtime"]])
                with runtime.open_image() as image:
                    self.assertEqual(list(image.size), [192, 192])
                    alpha_bounds = image.convert("RGBA").getchannel("A").getbbox()
                source_box = expected["sourceSolidBox"]
                self.assertIsNotNone(alpha_bounds)
                self.assertGreaterEqual(alpha_bounds[0], source_box["left"])
                self.assertGreaterEqual(alpha_bounds[1], source_box["top"])
                self.assertLessEqual(alpha_bounds[2], source_box["left"] + source_box["width"])
                self.assertLessEqual(alpha_bounds[3], source_box["top"] + source_box["height"])

                descriptor_path = IMAGES / f"{expected['resource']}.json"
                descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
                self.assertEqual(
                    descriptor["images"],
                    [{"group": 0, "frame": 0, "file": expected["runtime"]}],
                )
        patch = json.loads(ART_PATCH.read_text(encoding="utf-8"))
        self.assertEqual(patch["core:tower"]["town"]["structures"]["special2"]["x"], 402)

        # These engine-owned dynamic layers remain sourced from original map
        # frames; v3 changes only the authored opaque body images.
        generator = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
        map_renderer = (ROOT / "client/mapView/MapRenderer.cpp").read_text(encoding="utf-8")
        for resource, original in (
            ("NH_ACADEMY_VILLAGE_BODY", "AVCTOWR0"),
            ("NH_ACADEMY_FORT_BODY", "AVCTOWX0"),
            ("NH_ACADEMY_CAPITOL_BODY", "AVCTOWZ0"),
        ):
            self.assertIn(f'addAcademyMapLayers("{resource}", AnimationPath::builtin("{original}"))', generator)
        self.assertIn("ONLY_SHADOW_HIDE_FLAG_COLOR", generator)
        self.assertIn("ONLY_FLAG_COLOR", generator)
        self.assertIn("WITH_SHADOW_AND_FLAG_COLOR", map_renderer)

    def test_v3_loader_rejects_changed_manifest_or_export(self):
        pin = academy_importer.APPROVED_MAP_REVISION_V3_MANIFEST_SHA256
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            pin = private_revision_fixture(temp_root, "map", "v3")

            manifest_path = temp_root / academy_importer.MAP_REVISION_V3_MANIFEST
            manifest_bytes = manifest_path.read_bytes()
            loaded = academy_importer.load_map_revision(temp_root, pin, revision="v3")
            self.assertEqual(loaded["manifest_sha256"], pin)

            manifest_path.write_bytes(manifest_bytes + b" ")
            with self.assertRaisesRegex(RuntimeError, "manifest changed"):
                academy_importer.load_map_revision(temp_root, pin, revision="v3")
            manifest_path.write_bytes(manifest_bytes)

            altered_manifest = json.loads(manifest_bytes)
            export_path = temp_root / academy_importer.MAP_REVISION_V3_ROOT / "exports/NH_academy_village_body.png"
            export_bytes = export_path.read_bytes()
            export_path.write_bytes(export_bytes + b"\0")
            with self.assertRaisesRegex(ValueError, "export bytes do not match"):
                academy_importer.load_map_revision(temp_root, pin, revision="v3")

    def test_family_install_accepts_v2_and_legacy_then_rejects_unknown_atomically(self):
        runtimes = {record["runtime"] for record in academy_importer.MAP_REVISION_SLOTS.values()}
        v3 = shipping_revision("map", "v3")["exports_by_runtime"]
        # Installation preflight is byte-identity based; recognized prior bytes
        # are a synthetic fixture, not privately reconstructed v2 artwork.
        v2 = {runtime: f"synthetic prior:{runtime}".encode("ascii") for runtime in runtimes}
        legacy = {runtime: f"legacy:{runtime}".encode("ascii") for runtime in runtimes}

        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            # The installer must support a known mixed partial migration while
            # still preflighting every slot before it changes any file.
            initial = {}
            for index, runtime in enumerate(sorted(runtimes)):
                path = temp_root / academy_importer.IMAGE_ROOT / runtime
                path.parent.mkdir(parents=True, exist_ok=True)
                initial[runtime] = v2[runtime] if index != 1 else legacy[runtime]
                path.write_bytes(initial[runtime])

            academy_importer.install_curated_map_bodies(
                temp_root,
                v3,
                legacy,
                check_only=False,
                previously_approved_by_runtime=v2,
            )
            for runtime in runtimes:
                self.assertEqual((temp_root / academy_importer.IMAGE_ROOT / runtime).read_bytes(), v3[runtime])
            academy_importer.install_curated_map_bodies(
                temp_root,
                v3,
                legacy,
                check_only=True,
                previously_approved_by_runtime=v2,
            )

            before_unknown = {runtime: v3[runtime] for runtime in runtimes}
            for runtime in runtimes:
                (temp_root / academy_importer.IMAGE_ROOT / runtime).write_bytes(before_unknown[runtime])
            unknown_runtime = "NH_academy_fort_body.png"
            (temp_root / academy_importer.IMAGE_ROOT / unknown_runtime).write_bytes(b"unknown body pixels")
            before_unknown[unknown_runtime] = b"unknown body pixels"
            with self.assertRaisesRegex(RuntimeError, "unrecognized Academy map-body pixels"):
                academy_importer.install_curated_map_bodies(
                    temp_root,
                    v3,
                    legacy,
                    check_only=False,
                    previously_approved_by_runtime=v2,
                )
            for runtime, expected in before_unknown.items():
                self.assertEqual((temp_root / academy_importer.IMAGE_ROOT / runtime).read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
