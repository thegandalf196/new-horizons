#!/usr/bin/env python3
"""Focused validation/install checks for the provisional Academy landscape."""

from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_academy_background_v2 as background_exporter
import import_new_horizons_academy_assets as academy_importer


class NewHorizonsAcademyBackgroundImportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hall = academy_importer.load_hall_revision(
            ROOT,
            academy_importer.APPROVED_HALL_REVISION_MANIFEST_SHA256,
        )
        cls.background = academy_importer.load_background_revision(
            ROOT,
            academy_importer.APPROVED_BACKGROUND_REVISION_MANIFEST_SHA256,
            cls.hall,
        )

    def test_pinned_sources_and_export_match_mechanical_roi_registration(self):
        manifest = self.background["manifest"]
        self.assertEqual(self.background["manifest_sha256"], "2750379b86b4592235610060a2321993f067a3dec177c464b6e890a0c5942efd")
        self.assertEqual(manifest["sources"]["master"]["dimensions"], [1836, 857])
        self.assertEqual(manifest["sources"]["prompt"]["path"], "assets/new-horizons/academy/background-revisions/v2/landscape.prompt.txt")
        self.assertEqual(manifest["export"]["sha256"], "1a66adbbfc32d893ea6ec1bf19a7c8b09315615ebb1a4f48dc03b6946e31c45e")
        self.assertEqual(manifest["export"]["roi"], list(background_exporter.ROI))
        self.assertFalse(manifest["runtimeInstallation"])
        self.assertFalse(manifest["userVisualAcceptance"])
        self.assertEqual(
            manifest["sources"]["villageHallV2Overlay"]["placement"],
            {"x": 0, "y": 259, "z": 2},
        )

        baseline, exported = background_exporter.make_export()
        with Image.open(background_exporter.EXPORT_PATH) as pinned:
            pinned_rgb = pinned.convert("RGB")
        self.assertEqual(exported.tobytes(), pinned_rgb.tobytes())
        self.assertEqual(self.background["export_bytes"], background_exporter.EXPORT_PATH.read_bytes())
        self.assertEqual(self.background["baseline_native_bytes"], background_exporter.BASELINE_PATH.read_bytes())

        difference = ImageChops.difference(baseline, exported)
        left, top, right, bottom = background_exporter.ROI
        self.assertEqual(
            difference.crop((right, 0, baseline.width, baseline.height)).getbbox(),
            None,
        )
        self.assertEqual(difference.crop((0, 0, baseline.width, top)).getbbox(), None)
        self.assertEqual(difference.crop((0, bottom, baseline.width, baseline.height)).getbbox(), None)
        self.assertIsNotNone(difference.crop((left, top, right, bottom)).getbbox())

    def test_install_is_idempotent_and_rejects_unknown_or_wrong_baseline(self):
        baseline = self.background["baseline_native_bytes"]
        export = self.background["export_bytes"]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / academy_importer.IMAGE_ROOT / academy_importer.BACKGROUND_RUNTIME_IMAGE
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(baseline)

            with self.assertRaisesRegex(RuntimeError, "v2 is not installed"):
                academy_importer.install_curated_background(root, self.background, baseline, check_only=True)
            self.assertEqual(destination.read_bytes(), baseline)

            academy_importer.install_curated_background(root, self.background, baseline, check_only=False)
            self.assertEqual(destination.read_bytes(), export)
            academy_importer.install_curated_background(root, self.background, baseline, check_only=True)
            academy_importer.install_curated_background(root, self.background, baseline, check_only=False)
            self.assertEqual(destination.read_bytes(), export)

            destination.write_bytes(b"unknown local landscape")
            with self.assertRaisesRegex(RuntimeError, "unrecognized Academy town-background pixels"):
                academy_importer.install_curated_background(root, self.background, baseline, check_only=False)
            self.assertEqual(destination.read_bytes(), b"unknown local landscape")

            destination.write_bytes(baseline)
            with self.assertRaisesRegex(ValueError, "differs from the preserved native baseline"):
                academy_importer.install_curated_background(root, self.background, b"wrong archive landscape", check_only=False)
            self.assertEqual(destination.read_bytes(), baseline)


if __name__ == "__main__":
    unittest.main()
