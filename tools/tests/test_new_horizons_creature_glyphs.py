"""Check shipped creature glyph provenance, native dimensions and descriptors."""

import hashlib
import json
import struct
from pathlib import Path
import unittest

from tools.tests.nhart_test_resources import ArtPath


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/creature-stat-glyphs-v1"
RUNTIME = ArtPath("SPRITES")


class CreatureGlyphTests(unittest.TestCase):
    def test_shipping_export_hashes_dimensions_alpha_and_png_chunks(self):
        manifest = json.loads((SOURCE / "manifest.json").read_text())
        for group, directory in (("runtime", RUNTIME),):
            for filename, expected in manifest[group].items():
                with self.subTest(filename=filename):
                    path = directory / filename
                    data = path.read_bytes()
                    self.assertEqual(hashlib.sha256(data).hexdigest(), expected)
                    offset = 8
                    while offset < len(data):
                        size = struct.unpack(">I", data[offset:offset + 4])[0]
                        self.assertIn(data[offset + 4:offset + 8], (b"IHDR", b"IDAT", b"IEND"))
                        offset += size + 12
                    with path.open_image() as image:
                        self.assertEqual(image.mode, "RGBA")
                        self.assertEqual(list(image.size), manifest["dimensions"][group])
                        alpha_min, alpha_max = image.getchannel("A").getextrema()
                        self.assertEqual(alpha_min, 0)
                        # Thin 20px staircase strokes remain partially covered
                        # after LANCZOS reduction; do not threshold their alpha.
                        self.assertGreaterEqual(alpha_max, 230)

    def test_single_frame_descriptors(self):
        for subject in ("rank", "leadership"):
            stem = f"NH_creature_{subject}_20"
            descriptor = json.loads((RUNTIME / f"{stem}.json").read_text())
            self.assertEqual(descriptor, {"images": [
                {"group": 0, "frame": 0, "file": f"{stem}.png"}
            ]})

    def test_private_master_provenance_is_retained_as_metadata_not_shipping_input(self):
        manifest = json.loads((SOURCE / "manifest.json").read_text())
        self.assertEqual(manifest["masters"], {
            "rank-master.png": "23a1a79e53eac0fe442be9748e057ced9fa00b1d1b6b001c5bfb997ff403c1c1",
            "leadership-master.png": "6c83f35f9e74a90e673571555f9070cc878c7da852726150d54a3e9fc6dc9147",
        })
        self.assertEqual(manifest["dimensions"], {"masters": [1254, 1254], "runtime": [20, 20]})
        self.assertIn("LANCZOS", manifest["method"])


if __name__ == "__main__":
    unittest.main()
