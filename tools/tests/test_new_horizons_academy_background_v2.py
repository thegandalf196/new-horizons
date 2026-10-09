"""Landscape algorithm controls independent of private authoring masters."""
from hashlib import sha256
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_academy_background_v2 as exporter


class NewHorizonsAcademyBackgroundV2Test(unittest.TestCase):
    def test_native_export_is_opaque_and_changes_only_reviewed_roi(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            values = {}
            for name, image in (
                ("MASTER", Image.new("RGB", exporter.MASTER_SIZE, (40, 100, 160))),
                ("BASELINE", Image.new("RGB", exporter.NATIVE_SIZE, (90, 50, 20))),
                ("HALL_V2", Image.new("RGBA", (180, 110), (80, 120, 60, 255))),
            ):
                path = root / (name + ".png")
                image.save(path)
                values[name + "_PATH"] = path
                values["EXPECTED_" + name + "_SHA256"] = sha256(path.read_bytes()).hexdigest()
            prompt = root / "prompt.txt"
            prompt.write_text("synthetic algorithm fixture")
            values.update(PROMPT_PATH=prompt, EXPECTED_PROMPT_SHA256=sha256(prompt.read_bytes()).hexdigest())
            with patch.multiple(exporter, **values):
                baseline, exported = exporter.make_export()
                self.assertEqual(exporter.png_bytes(exported), exporter.png_bytes(exporter.make_export()[1]))
                self.assertEqual(exported.size, (800, 374))
                self.assertEqual(exported.mode, "RGB")
                self.assertEqual(ImageChops.difference(baseline, exported).getbbox(), exporter.ROI)
                self.assertEqual(exported.getpixel((10, 270)), (40, 100, 160))
                self.assertEqual(exported.getpixel((799, 373)), (90, 50, 20))
                prompt.write_text("changed input")
                with self.assertRaisesRegex(ValueError, "Pinned landscape prompt changed"):
                    exporter.make_export()

    def test_comparisons_preserve_native_and_nearest_detail_dimensions(self):
        original = Image.new("RGB", exporter.NATIVE_SIZE, (10, 20, 30))
        proposed = original.copy()
        proposed.paste((40, 50, 60), exporter.ROI)
        self.assertEqual(exporter.make_comparison(original, proposed).size, (1608, 374))
        detail = exporter.make_roi_comparison(original, proposed)
        self.assertGreater(detail.width, 2 * 205)
        self.assertGreaterEqual(detail.height, 4 * 108)

    def test_authoring_outputs_require_an_explicit_external_root(self):
        with self.assertRaisesRegex(ValueError, "explicit private project root"):
            exporter.require_private_outputs()
        with self.assertRaisesRegex(ValueError, "outside and not overlapping"):
            exporter.configure_private_root(ROOT / "build")
