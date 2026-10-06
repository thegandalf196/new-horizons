#!/usr/bin/env python3
"""Focused wiring and source-exactness checks for Academy Gremlin portraits."""

import json
from pathlib import Path
import subprocess
import sys
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGE_ROOT = ROOT / "Mods/new-horizons/Images"
EXPORTER = ROOT / "tools/export_new_horizons_academy_gremlin_portraits.py"


class NewHorizonsGremlinPortraitTest(unittest.TestCase):
    def test_runtime_exports_match_authored_backdrop_and_approved_masks(self):
        subprocess.run([sys.executable, str(EXPORTER), "--check"], cwd=ROOT, check=True)

        backdrop_paths = (
            "NH_academy_creature_portrait_backdrop.png",
            "NH_academy_gremlin_icon_large.png",
            "NH_academy_masterGremlin_icon_large.png",
        )
        decoded = []
        for path in backdrop_paths:
            with Image.open(IMAGE_ROOT / path) as image:
                self.assertEqual(image.mode, "RGB")
                self.assertEqual(image.size, (58, 64))
                decoded.append(image.tobytes())
        self.assertTrue(all(pixels == decoded[0] for pixels in decoded[1:]))

        for creature in ("gremlin", "masterGremlin"):
            runtime = IMAGE_ROOT / f"NH_academy_{creature}_portrait_mask.png"
            source = ROOT / f"assets/new-horizons/academy/portrait-revisions/v1/mattes/{creature}.png"
            self.assertEqual(runtime.read_bytes(), source.read_bytes())

    def test_only_reviewed_large_gremlin_portrait_routes_are_registered(self):
        config = json.loads((ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text())
        expected = {
            "core:gremlin": "NH_academy_gremlin_icon_large.png",
            "core:masterGremlin": "NH_academy_masterGremlin_icon_large.png",
        }
        for creature, image in expected.items():
            with self.subTest(creature=creature):
                graphics = config[creature]["graphics"]
                self.assertEqual(graphics, {"iconLarge": image})
                self.assertTrue((IMAGE_ROOT / image).is_file())

        generator = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
        for image in expected.values():
            self.assertIn(f'ImagePath::builtin("{image}")', generator)
        self.assertNotIn("NH_academy_stoneGargoyle_icon_large.png", generator)
        self.assertNotIn("NH_academy_obsidianGargoyle_icon_large.png", generator)


if __name__ == "__main__":
    unittest.main()
