#!/usr/bin/env python3
"""Focused wiring and source-exactness checks for Academy large portraits."""

import json
from pathlib import Path
import re
import subprocess
import sys
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGE_ROOT = ROOT / "Mods/new-horizons/Images"
EXPORTER = ROOT / "tools/export_new_horizons_academy_gremlin_portraits.py"
STRING = r'"(?:\\.|[^"\\])*"'


def parse_jsonc(text):
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    return json.loads(text)


class NewHorizonsGremlinPortraitTest(unittest.TestCase):
    def test_runtime_exports_match_authored_backdrop_and_approved_masks(self):
        subprocess.run([sys.executable, str(EXPORTER), "--check"], cwd=ROOT, check=True)

        backdrop_paths = (
            "NH_academy_creature_portrait_backdrop.png",
            "NH_academy_gremlin_icon_large.png",
            "NH_academy_masterGremlin_icon_large.png",
            "NH_academy_ironGolem_icon_large.png",
            "NH_academy_stoneGolem_icon_large.png",
            "NH_academy_mage_icon_large.png",
            "NH_academy_archMage_icon_large.png",
        )
        decoded = []
        for path in backdrop_paths:
            with Image.open(IMAGE_ROOT / path) as image:
                self.assertEqual(image.mode, "RGB")
                self.assertEqual(image.size, (58, 64))
                decoded.append(image.tobytes())
        self.assertTrue(all(pixels == decoded[0] for pixels in decoded[1:]))

        for creature in ("gremlin", "masterGremlin", "ironGolem", "stoneGolem", "mage", "archMage"):
            runtime = IMAGE_ROOT / f"NH_academy_{creature}_portrait_mask.png"
            source = ROOT / f"assets/new-horizons/academy/portrait-revisions/v1/mattes/{creature}.png"
            self.assertEqual(runtime.read_bytes(), source.read_bytes())

    def test_only_reviewed_large_portrait_routes_are_registered(self):
        config = parse_jsonc((ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text())
        expected = {
            "core:gremlin": "NH_academy_gremlin_icon_large.png",
            "core:masterGremlin": "NH_academy_masterGremlin_icon_large.png",
            "core:ironGolem": "NH_academy_ironGolem_icon_large.png",
            "core:stoneGolem": "NH_academy_stoneGolem_icon_large.png",
            "core:mage": "NH_academy_mage_icon_large.png",
            "core:archMage": "NH_academy_archMage_icon_large.png",
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

    def test_golem_portrait_frame_ids_preserve_hottraits_reversal(self):
        core = parse_jsonc((ROOT / "config/creatures/tower.json").read_text())
        self.assertEqual(core["ironGolem"]["index"], 32)
        self.assertEqual(core["stoneGolem"]["index"], 33)
        self.assertEqual(core["mage"]["index"], 34)
        self.assertEqual(core["archMage"]["index"], 35)
        fixture = (ROOT / "client/tests/AcademyBuiltIconRuntimeTest.cpp").read_text(encoding="utf-8")
        self.assertIn('{"ironGolem", "NH_academy_ironGolem_icon_large.png", "NH_academy_ironGolem_portrait_mask.png", 32, 34}', fixture)
        self.assertIn('{"stoneGolem", "NH_academy_stoneGolem_icon_large.png", "NH_academy_stoneGolem_portrait_mask.png", 33, 35}', fixture)
        self.assertIn('{"mage", "NH_academy_mage_icon_large.png", "NH_academy_mage_portrait_mask.png", 34, 36}', fixture)
        self.assertIn('{"archMage", "NH_academy_archMage_icon_large.png", "NH_academy_archMage_portrait_mask.png", 35, 37}', fixture)
        config = (ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text(encoding="utf-8")
        self.assertIn("frame 34 is internal ironGolem ID 32; frame 35 is stoneGolem ID 33", config)


if __name__ == "__main__":
    unittest.main()
