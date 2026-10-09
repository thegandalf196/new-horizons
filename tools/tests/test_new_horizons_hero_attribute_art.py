"""Verify hero attribute art provenance, descriptor slots and native exports."""

import hashlib
import json
from pathlib import Path
import struct
import unittest

if __package__:
    from .nhart_test_resources import ArtPath
else:
    from nhart_test_resources import ArtPath


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/hero-attribute-replacements-v1"
RUNTIME = ArtPath()


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

    def test_retained_provenance_matches_every_shipping_export(self):
        self.assertEqual(set(self.manifest["masters"]), {"movement-master.png", "leadership-master.png"})
        for filename, sha256 in self.manifest["masters"].items():
            with self.subTest(filename=filename):
                self.assertEqual(len(sha256), 64)
                self.assertEqual(set(sha256) - set("0123456789abcdef"), set())
                self.assertTrue(any(entry["source"] == filename for entry in self.manifest["runtime"].values()))

    def test_runtime_slots_and_exact_export_pixels(self):
        for filename, entry in self.manifest["runtime"].items():
            with self.subTest(filename=filename):
                path = RUNTIME / filename
                self.assert_clean_png(path, entry["sha256"])
                self.assertIn(entry["source"], self.manifest["masters"])
                with path.open_image() as image:
                    self.assertEqual(image.mode, "RGB")
                    self.assertEqual(image.size, (entry["size"], entry["size"]))
                descriptor = json.loads(path.with_suffix(".json").read_text())
                self.assertEqual(descriptor, {"images": [
                    {"group": 0, "frame": 0, "file": filename}
                ]})


if __name__ == "__main__":
    unittest.main()
