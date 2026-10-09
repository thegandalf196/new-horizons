"""Pixel-level checks for three purpose-made Metamagic rank paintings."""

import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

from PIL import Image
if __package__:
    from .nhart_test_resources import open_image, read_resource
else:
    from nhart_test_resources import open_image, read_resource


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/metamagic-prisms-v2"
SPEC = importlib.util.spec_from_file_location("metamagic_export", SOURCE.parent / "export_metamagic_prisms.py")
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)


class MetamagicExportTests(unittest.TestCase):
    def test_twelve_shipping_native_slots_preserve_hashes_and_centered_geometry(self):
        manifest = json.loads((SOURCE / "manifest.json").read_text())
        self.assertEqual(set(manifest["ranks"]), {"basic", "advanced", "expert"})
        for rank, entry in manifest["ranks"].items():
            self.assertEqual(entry["dimensions"], [1254, 1254])
            self.assertEqual(len(entry["sha256"]), 64)
            self.assertEqual(set(entry["outputs"]), {"small", "medium", "large", "scenarioBonus"})
            for slot, output in entry["outputs"].items():
                with self.subTest(rank=rank, slot=slot):
                    payload = read_resource("SPRITES/" + output["file"])
                    self.assertEqual(hashlib.sha256(payload).hexdigest(), output["sha256"])
                    width, height = output["dimensions"]
                    offset = (height - width) // 2
                    self.assertEqual(output["offset"], [0, offset])
                    with open_image("SPRITES/" + output["file"]) as actual:
                        self.assertEqual(actual.size, (width, height))
                        self.assertEqual(actual.mode, "RGBA")
                        square = actual.crop((0, offset, width, offset + width))
                        expected = EXPORTER.fit_export(square, (width, height))
                        self.assertEqual(actual.tobytes(), expected.tobytes())

    def test_exporter_centers_synthetic_native_pixels_without_resizing_or_alpha_mask(self):
        for dimensions in EXPORTER.SLOTS.values():
            width, height = dimensions
            square = Image.new("RGBA", (width, width), (31, 67, 109, 127))
            square.putpixel((0, 0), (5, 7, 11, 0))
            actual = EXPORTER.fit_export(square, dimensions)
            offset = (height - width) // 2
            self.assertEqual(actual.crop((0, offset, width, offset + width)).tobytes(), square.tobytes())
            self.assertEqual(actual.size, dimensions)
            for row in list(range(offset)) + list(range(offset + width, height)):
                self.assertEqual(actual.crop((0, row, width, row + 1)).getchannel("A").getextrema(), (0, 0))
        with self.assertRaises(ValueError):
            EXPORTER.fit_export(Image.new("RGBA", (31, 32)), (32, 32))


if __name__ == "__main__":
    unittest.main()
