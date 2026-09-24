"""Pixel-level checks for three purpose-made Metamagic rank paintings."""

import hashlib
import json
from pathlib import Path
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/new-horizons/art-source/metamagic-prisms-v2"
IMAGES = ROOT / "Mods/new-horizons/Images"


class MetamagicExportTests(unittest.TestCase):
    def test_twelve_native_slots_reproduce_without_stretching(self):
        manifest = json.loads((SOURCE / "manifest.json").read_text())
        self.assertEqual(set(manifest["ranks"]), {"basic", "advanced", "expert"})
        for rank, entry in manifest["ranks"].items():
            master_path = SOURCE / entry["master"]
            self.assertEqual(hashlib.sha256(master_path.read_bytes()).hexdigest(), entry["sha256"])
            with Image.open(master_path) as opened:
                master = opened.copy()
            self.assertEqual(master.size, (1254, 1254))
            if rank in ("advanced", "expert"):
                self.assertEqual(master.mode, "RGB", "painted background must be opaque")
            self.assertEqual(set(entry["outputs"]), {"small", "medium", "large", "scenarioBonus"})
            for slot, output in entry["outputs"].items():
                with self.subTest(rank=rank, slot=slot):
                    target = IMAGES / output["file"]
                    self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(), output["sha256"])
                    width, height = output["dimensions"]
                    expected = Image.new("RGBA", (width, height), (0, 0, 0, 0))
                    square = master.resize((width, width), Image.Resampling.LANCZOS).convert("RGBA")
                    expected.paste(square, (0, (height - width) // 2))
                    with Image.open(target) as actual:
                        self.assertEqual(actual.size, expected.size)
                        self.assertEqual(actual.mode, "RGBA")
                        self.assertEqual(actual.tobytes(), expected.tobytes())


if __name__ == "__main__":
    unittest.main()
