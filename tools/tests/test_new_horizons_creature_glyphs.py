"""Check shipped creature glyph provenance, native dimensions and descriptors."""

import hashlib
import json
import struct
from pathlib import Path
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/creature-stat-glyphs-v1"
RUNTIME = ROOT / "Mods/new-horizons/Images"


class CreatureGlyphTests(unittest.TestCase):
    def test_retained_master_and_export_hashes(self):
        manifest = json.loads((SOURCE / "manifest.json").read_text())
        for group, directory in (("masters", SOURCE), ("runtime", RUNTIME)):
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
                    with Image.open(path) as image:
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

    def test_export_pixels_reproduce_from_master(self):
        for subject in ("rank", "leadership"):
            with self.subTest(subject=subject):
                with Image.open(SOURCE / f"{subject}-master.png") as master:
                    expected = master.resize((20, 20), Image.Resampling.LANCZOS)
                with Image.open(RUNTIME / f"NH_creature_{subject}_20.png") as actual:
                    self.assertEqual(actual.tobytes(), expected.tobytes())


if __name__ == "__main__":
    unittest.main()
