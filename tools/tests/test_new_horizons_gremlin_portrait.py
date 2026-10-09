#!/usr/bin/env python3
"""Shipping Academy portrait checks; authoring algorithms use synthetic inputs."""

import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest

from PIL import Image, ImageChops

from tools.export_new_horizons_academy_portrait_mattes import render_matte
from tools.tests.nhart_test_resources import ArtPath

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ArtPath("SPRITES")
STRING = r'"(?:\\.|[^"\\])*"'


def parse_jsonc(text):
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    return json.loads(text)


class NewHorizonsGremlinPortraitTest(unittest.TestCase):
    def test_shipping_backdrops_and_reviewed_binary_masks(self):
        creatures = ("gremlin", "masterGremlin", "ironGolem", "stoneGolem", "mage", "archMage",
                     "genie", "masterGenie", "naga", "nagaQueen", "giant", "titan")
        with (IMAGES / "NH_academy_creature_portrait_backdrop.png").open_image() as backdrop:
            self.assertEqual((backdrop.mode, backdrop.size), ("RGB", (58, 64)))
            pixels = backdrop.tobytes()
        for creature in creatures:
            with self.subTest(creature=creature):
                with (IMAGES / f"NH_academy_{creature}_icon_large.png").open_image() as image:
                    self.assertEqual((image.mode, image.size), ("RGB", (58, 64)))
                    self.assertEqual(image.tobytes(), pixels)
                with (IMAGES / f"NH_academy_{creature}_portrait_mask.png").open_image() as mask:
                    self.assertEqual((mask.mode, mask.size), ("L", (58, 64)))
                    self.assertEqual(set(mask.tobytes()), {0, 255})

        pinned = {
            "masterGenie": "9f5291e7d50b29a4a16d8c54aeba2de9b4566456d5b5823e4d18023ac6659a40",
            "naga": "c4e58bcf3c9b84f63ec7137c37da23fe3bc898a0e77e653ee6ad016c3997e453",
            "nagaQueen": "aa282c71f59b720ccc2604e81b3bc1527b8e58b7ab60403eee9fb28eb9f64c30",
            "giant": "5cb48b0f40db757569d6e500d2d199c9724d10d942942e6322f5dfecef1f29dc",
            "titan": "df23c30a56d0f08f7b24e16d05d6de2e55acedfa75bcbfe3143577f5fc3b8092",
        }
        for creature, digest in pinned.items():
            self.assertEqual(hashlib.sha256((IMAGES / f"NH_academy_{creature}_portrait_mask.png").read_bytes()).hexdigest(), digest)

    def test_matte_reduction_and_local_sword_addition_are_synthetic(self):
        # Authoring masters intentionally are not required by a shipping checkout.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic-master.png"
            master = Image.new("L", (116, 128))
            master.paste(255, (12, 8, 40, 104))
            master.save(path)
            reduced = render_matte(path)
            expected = master.resize((58, 64), Image.Resampling.LANCZOS).point(
                lambda value: 255 if value >= 128 else 0, mode="L")
            self.assertEqual(reduced.tobytes(), expected.tobytes())
            old = Image.new("L", (58, 64))
            old.paste(255, (20, 20, 30, 50))
            roi = (4, 0, 11, 28)
            combined = old.copy()
            combined.paste(ImageChops.lighter(old.crop(roi), reduced.crop(roi)), roi)
            self.assertNotEqual(combined.tobytes(), old.tobytes())
            for y in range(64):
                for x in range(58):
                    self.assertGreaterEqual(combined.getpixel((x, y)), old.getpixel((x, y)))
                    if not (roi[0] <= x < roi[2] and roi[1] <= y < roi[3]):
                        self.assertEqual(combined.getpixel((x, y)), old.getpixel((x, y)))

    def test_selected_cabir_portraits_are_shipped_at_native_sizes(self):
        config = parse_jsonc((ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text())
        for creature, subject in (("core:gremlin", "cabir"), ("core:masterGremlin", "cabir-master")):
            for key, suffix, size in (("iconLarge", "large", (58, 64)), ("iconSmall", "small", (32, 32))):
                route = config[creature]["graphics"][key]
                self.assertEqual(route, f"cabir-handoff/{subject}/icons/NH_{subject.replace('-', '_')}_handoff_icon_{suffix}.png")
                with (IMAGES / route).open_image() as image:
                    self.assertEqual((image.mode, image.size), ("RGBA", size))

    def test_selected_large_portrait_routes_have_shipping_payloads(self):
        config = parse_jsonc((ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text())
        expected = {name: f"NH_academy_{name}_icon_large.png" for name in
                    ("stoneGargoyle", "obsidianGargoyle", "ironGolem", "stoneGolem", "genie", "masterGenie", "naga", "nagaQueen", "giant", "titan")}
        expected.update(mage="NH_academy_mageHolding_icon_large.png", archMage="magi-vcmi-complete/icons/archmagi-portrait-58x64.png")
        generator = (ROOT / "client/render/AssetGenerator.cpp").read_text()
        for creature, route in expected.items():
            self.assertEqual(config[f"core:{creature}"]["graphics"]["iconLarge"], route)
            if creature not in ("stoneGargoyle", "obsidianGargoyle", "mage"):
                self.assertTrue((IMAGES / route).is_file(), route)
                with (IMAGES / route).open_image() as image:
                    self.assertEqual(image.size, (58, 64))
            if route.startswith("NH_academy_"):
                self.assertIn(f'ImagePath::builtin("{route}")', generator)
        # These routes are generated, not missing packed PNGs. Gargoyle bodies
        # deliberately remain purchaser-original CGARGO/COGARG frames.
        self.assertIn('AnimationPath::builtin("CGARGO")', generator)
        self.assertIn('AnimationPath::builtin("COGARG")', generator)
        holding = "magi-vcmi-complete/battle/magi/idle/000.png"
        self.assertIn(f'ImagePath::builtin("{holding}")', generator)
        self.assertTrue((IMAGES / holding).is_file())

    def test_portrait_frame_ids_preserve_hottraits_reversal(self):
        core = parse_jsonc((ROOT / "config/creatures/tower.json").read_text())
        creatures = ("ironGolem", "stoneGolem", "mage", "archMage", "genie", "masterGenie", "naga", "nagaQueen", "giant", "titan")
        for index, creature in enumerate(creatures, 32):
            self.assertEqual(core[creature]["index"], index)
        config = (ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text()
        self.assertIn("frame 34 is internal ironGolem ID 32; frame 35 is stoneGolem ID 33", config)
        generator = (ROOT / "client/render/AssetGenerator.cpp").read_text()
        self.assertIn('createAcademyCreaturePortrait(34, "NH_academy_ironGolem_portrait_mask.png")', generator)
        self.assertIn('createAcademyCreaturePortrait(35, "NH_academy_stoneGolem_portrait_mask.png")', generator)


if __name__ == "__main__":
    unittest.main()
