"""Verify hero attribute art provenance, descriptor slots and native exports."""

import hashlib
import json
from pathlib import Path
import struct
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/hero-attribute-replacements-v1"
RUNTIME = ROOT / "Mods/new-horizons/Images"


class HeroAttributeArtTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((SOURCE / "manifest.json").read_text())

    def assert_clean_png(self, path, expected_hash):
        data = path.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), expected_hash)
        offset = 8
        while offset < len(data):
            length = struct.unpack(">I", data[offset:offset + 4])[0]
            self.assertIn(data[offset + 4:offset + 8], (b"IHDR", b"IDAT", b"IEND"))
            offset += length + 12

    def test_master_provenance_and_opaque_canvas(self):
        for filename, sha256 in self.manifest["masters"].items():
            with self.subTest(filename=filename):
                path = SOURCE / filename
                self.assert_clean_png(path, sha256)
                with Image.open(path) as image:
                    self.assertEqual(image.mode, "RGB")
                    self.assertEqual(image.size, (1254, 1254))

    def test_runtime_slots_and_exact_export_pixels(self):
        for filename, entry in self.manifest["runtime"].items():
            with self.subTest(filename=filename):
                path = RUNTIME / filename
                self.assert_clean_png(path, entry["sha256"])
                with Image.open(SOURCE / entry["source"]) as master:
                    expected = master.resize((entry["size"], entry["size"]), Image.Resampling.LANCZOS)
                with Image.open(path) as image:
                    self.assertEqual(image.mode, "RGB")
                    self.assertEqual(image.size, expected.size)
                    self.assertEqual(image.tobytes(), expected.tobytes())
                descriptor = json.loads(path.with_suffix(".json").read_text())
                self.assertEqual(descriptor, {"images": [
                    {"group": 0, "frame": 0, "file": filename}
                ]})


if __name__ == "__main__":
    unittest.main()
