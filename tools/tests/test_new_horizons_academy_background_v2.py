"""Focused checks for the bounded Academy landscape v2 export."""

from __future__ import annotations

from io import BytesIO
import json
from pathlib import Path
import sys
import unittest

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_academy_background_v2 as exporter


class NewHorizonsAcademyBackgroundV2Test(unittest.TestCase):
    def test_native_export_is_opaque_and_changes_only_reviewed_roi(self):
        baseline, exported = exporter.make_export()

        self.assertEqual(baseline.size, (800, 374))
        self.assertEqual(exported.size, (800, 374))
        self.assertEqual(exported.mode, "RGB")
        self.assertEqual(exporter.ROI, (0, 254, 177, 335))

        master = exporter.open_rgb(exporter.MASTER_PATH)
        resized = master.resize(exporter.NATIVE_SIZE, Image.Resampling.LANCZOS)
        self.assertEqual(
            exported.crop(exporter.ROI).tobytes(),
            resized.crop(exporter.ROI).tobytes(),
        )

        left, top, right, bottom = exporter.ROI
        self.assertEqual(baseline.crop((right, 0, baseline.width, baseline.height)).tobytes(),
                         exported.crop((right, 0, exported.width, exported.height)).tobytes())
        self.assertEqual(baseline.crop((0, 0, baseline.width, top)).tobytes(),
                         exported.crop((0, 0, exported.width, top)).tobytes())
        self.assertEqual(baseline.crop((0, bottom, baseline.width, baseline.height)).tobytes(),
                         exported.crop((0, bottom, exported.width, exported.height)).tobytes())

        difference = ImageChops.difference(baseline, exported)
        changed_bounds = difference.getbbox()
        self.assertIsNotNone(changed_bounds)
        self.assertGreaterEqual(changed_bounds[0], left)
        self.assertGreaterEqual(changed_bounds[1], top)
        self.assertLessEqual(changed_bounds[2], right)
        self.assertLessEqual(changed_bounds[3], bottom)

    def test_registered_scene_uses_one_village_hall_and_no_later_hall_stage(self):
        layers = exporter.effective_village_hall_layers()
        hall_layers = [row for row in layers if row[2] == "villageHall"]
        self.assertEqual(len(hall_layers), 1)
        z, _sequence, _name, x, y, _path = hall_layers[0]
        self.assertEqual((x, y, z), (0, 259, 2))
        self.assertFalse(any(row[2] in {"townHall", "cityHall", "capitol"} for row in layers))

        artifacts = exporter.build_artifacts()
        manifest = json.loads(artifacts[exporter.MANIFEST_PATH].decode("utf-8"))
        self.assertFalse(manifest["runtimeInstallation"])
        self.assertIn("not a game screenshot", manifest["registeredSceneNotice"])
        with Image.open(BytesIO(artifacts[exporter.SCENE_COMPARISON_PATH])) as scene:
            self.assertEqual(scene.size, (1608, 374))

    def test_exports_manifest_and_comparisons_are_reproducible(self):
        for path, expected in exporter.build_artifacts().items():
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertTrue(path.is_file(), f"Missing generated artifact: {path}")
                self.assertEqual(path.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
