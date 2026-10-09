#!/usr/bin/env python3
"""Focused validation/install checks for the provisional Academy landscape."""

from pathlib import Path
import sys
import tempfile
import unittest
from tools.tests.nhart_test_resources import open_image, read_resource

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import import_new_horizons_academy_assets as academy_importer


class NewHorizonsAcademyBackgroundImportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Installation is a byte identity gate, independent of private masters.
        cls.background = {"baseline_native_bytes": b"synthetic original landscape",
                          "export_bytes": read_resource("SPRITES/NH_academy/town/landscape.png")}

    def test_selected_runtime_landscape_has_native_dimensions(self):
        image = open_image("SPRITES/NH_academy/town/landscape.png")
        self.assertEqual(image.size, (800, 374))
        self.assertEqual(image.mode, "RGB")
        self.assertEqual(academy_importer.BACKGROUND_RUNTIME_IMAGE, "NH_academy/town/landscape.png")

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

            destination.unlink()
            outside = root / "unrelated.png"
            outside.write_bytes(b"untouched")
            destination.symlink_to(outside)
            with self.assertRaisesRegex(RuntimeError, "symlinked Academy town background"):
                academy_importer.install_curated_background(root, self.background, baseline, check_only=False)
            self.assertEqual(outside.read_bytes(), b"untouched")
            destination.unlink()
            destination.write_bytes(baseline)

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
