#!/usr/bin/env python3
"""Focused wiring and source-exactness checks for Academy large portraits."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import unittest

from PIL import Image, ImageChops

from tools.export_new_horizons_academy_portrait_mattes import render_matte


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
            "NH_academy_genie_icon_large.png",
            "NH_academy_masterGenie_icon_large.png",
            "NH_academy_naga_icon_large.png",
            "NH_academy_nagaQueen_icon_large.png",
            "NH_academy_giant_icon_large.png",
            "NH_academy_titan_icon_large.png",
        )
        decoded = []
        for path in backdrop_paths:
            with Image.open(IMAGE_ROOT / path) as image:
                self.assertEqual(image.mode, "RGB")
                self.assertEqual(image.size, (58, 64))
                decoded.append(image.tobytes())
        self.assertTrue(all(pixels == decoded[0] for pixels in decoded[1:]))

        creatures = ("gremlin", "masterGremlin", "ironGolem", "stoneGolem", "mage", "archMage", "genie", "naga")
        for creature in creatures:
            runtime = IMAGE_ROOT / f"NH_academy_{creature}_portrait_mask.png"
            source = ROOT / f"assets/new-horizons/academy/portrait-revisions/v1/mattes/{creature}.png"
            self.assertEqual(runtime.read_bytes(), source.read_bytes())
        master_genie_source = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/masterGenie-v2.png"
        master_genie_bytes = master_genie_source.read_bytes()
        self.assertEqual(
            hashlib.sha256(master_genie_bytes).hexdigest(),
            "9f5291e7d50b29a4a16d8c54aeba2de9b4566456d5b5823e4d18023ac6659a40",
        )
        self.assertEqual(
            (IMAGE_ROOT / "NH_academy_masterGenie_portrait_mask.png").read_bytes(), master_genie_bytes
        )
        naga_source = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/naga.png"
        naga_bytes = naga_source.read_bytes()
        self.assertEqual(
            hashlib.sha256(naga_bytes).hexdigest(),
            "c4e58bcf3c9b84f63ec7137c37da23fe3bc898a0e77e653ee6ad016c3997e453",
        )
        self.assertEqual((IMAGE_ROOT / "NH_academy_naga_portrait_mask.png").read_bytes(), naga_bytes)
        naga_queen_source = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/nagaQueen-v2.png"
        naga_queen_bytes = naga_queen_source.read_bytes()
        self.assertEqual(
            hashlib.sha256(naga_queen_bytes).hexdigest(),
            "aa282c71f59b720ccc2604e81b3bc1527b8e58b7ab60403eee9fb28eb9f64c30",
        )
        self.assertEqual(
            (IMAGE_ROOT / "NH_academy_nagaQueen_portrait_mask.png").read_bytes(), naga_queen_bytes
        )
        giant_source = ROOT / "assets/new-horizons/academy/portrait-revisions/v4/mattes/giant-sword.png"
        giant_bytes = giant_source.read_bytes()
        self.assertEqual(
            hashlib.sha256(giant_bytes).hexdigest(),
            "5cb48b0f40db757569d6e500d2d199c9724d10d942942e6322f5dfecef1f29dc",
        )
        self.assertEqual((IMAGE_ROOT / "NH_academy_giant_portrait_mask.png").read_bytes(), giant_bytes)
        titan_source = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/titan-v3.png"
        titan_bytes = titan_source.read_bytes()
        self.assertEqual(
            hashlib.sha256(titan_bytes).hexdigest(),
            "df23c30a56d0f08f7b24e16d05d6de2e55acedfa75bcbfe3143577f5fc3b8092",
        )
        self.assertEqual((IMAGE_ROOT / "NH_academy_titan_portrait_mask.png").read_bytes(), titan_bytes)

    def test_giant_sword_revision_preserves_body_and_generated_provenance(self):
        revision = ROOT / "assets/new-horizons/academy/portrait-revisions/v4"
        old_path = ROOT / "assets/new-horizons/academy/portrait-revisions/v1/mattes/giant.png"
        self.assertEqual(hashlib.sha256(old_path.read_bytes()).hexdigest(),
                         "2e576469ff77e6c07ad529caedead80bc342e39f7fd5c87bd293dd53a83102bd")
        with Image.open(old_path) as old_image, Image.open(revision / "mattes/giant-sword.png") as new_image:
            self.assertEqual(new_image.mode, "L")
            self.assertEqual(new_image.size, (58, 64))
            old = old_image.copy()
            new = new_image.copy()
        roi = (4, 0, 11, 28)
        generated = render_matte(revision / "masters/giant-sword-matte.png")
        expected = old.copy()
        expected.paste(ImageChops.lighter(old.crop(roi), generated.crop(roi)), roi)
        self.assertEqual(new.tobytes(), expected.tobytes())
        additions = []
        for y in range(64):
            for x in range(58):
                previous, current = old.getpixel((x, y)), new.getpixel((x, y))
                self.assertIn(current, (0, 255))
                self.assertGreaterEqual(current, previous)
                if previous != current:
                    additions.append((x, y))
                    self.assertTrue(roi[0] <= x < roi[2] and roi[1] <= y < roi[3])
        self.assertEqual(len(additions), 73)

    def test_cabir_portraits_use_authored_v3_derivatives(self):
        config = parse_jsonc((ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text())
        expected = {
            "core:gremlin": {
                "large": "NH_cabir_icon_large.png",
                "small": "NH_cabir_icon_small.png",
                "sourceDirectory": ROOT / "assets/new-horizons/creatures/cabir/v3/portrait-export-v1",
            },
            "core:masterGremlin": {
                "large": "NH_cabirMaster_icon_large.png",
                "small": "NH_cabirMaster_icon_small.png",
                "sourceDirectory": ROOT / "assets/new-horizons/creatures/cabir-master/v3/portrait-export-v1",
            },
        }
        for creature, paths in expected.items():
            with self.subTest(creature=creature):
                graphics = config[creature]["graphics"]
                self.assertEqual(
                    {key: graphics[key] for key in ("iconLarge", "iconSmall")},
                    {"iconLarge": paths["large"], "iconSmall": paths["small"]},
                )
                for key, size in (("large", (58, 64)), ("small", (32, 32))):
                    runtime_path = IMAGE_ROOT / paths[key]
                    authored_path = paths["sourceDirectory"] / paths[key]
                    self.assertTrue(runtime_path.is_file())
                    self.assertEqual(runtime_path.read_bytes(), authored_path.read_bytes())
                    with Image.open(runtime_path) as image:
                        self.assertEqual(image.mode, "RGB")
                        self.assertEqual(image.size, size)

        # The existing AssetGenerator portrait-mask paths remain compatibility
        # routes for old content; these assertions intentionally do not require
        # those legacy handlers to be removed.

    def test_only_reviewed_large_portrait_routes_are_registered(self):
        config = parse_jsonc((ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text())
        expected = {
            "core:ironGolem": "NH_academy_ironGolem_icon_large.png",
            "core:stoneGolem": "NH_academy_stoneGolem_icon_large.png",
            "core:mage": "NH_academy_mage_icon_large.png",
            "core:archMage": "NH_academy_archMage_icon_large.png",
            "core:genie": "NH_academy_genie_icon_large.png",
            "core:masterGenie": "NH_academy_masterGenie_icon_large.png",
            "core:naga": "NH_academy_naga_icon_large.png",
            "core:nagaQueen": "NH_academy_nagaQueen_icon_large.png",
            "core:giant": "NH_academy_giant_icon_large.png",
            "core:titan": "NH_academy_titan_icon_large.png",
        }
        for creature, image in expected.items():
            with self.subTest(creature=creature):
                graphics = config[creature]["graphics"]
                self.assertEqual(graphics["iconLarge"], image)
                expected_fields = {"iconLarge"}
                if creature == "core:mage":
                    expected_fields.add("missile")
                    self.assertEqual(graphics["missile"], {"projectile": "NH_MageRedProjectile.def"})
                elif creature == "core:archMage":
                    expected_fields.update(("animation", "missile", "map", "mapMask", "iconSmall", "mapAttackFromRight", "mapAttackFromLeft"))
                    self.assertEqual(graphics["animation"], "NH_ArchMageGrey.def")
                    self.assertEqual(graphics["map"], "NH_ArchMageGreyMap.def")
                    self.assertEqual(graphics["mapMask"], ["VV", "VA"])
                    self.assertEqual(graphics["iconSmall"], "NH_ArchMageGreySmall:0:0")
                    self.assertEqual(graphics["mapAttackFromRight"], "NH_ArchMageGreyEncounter:0:0")
                    self.assertEqual(graphics["mapAttackFromLeft"], "NH_ArchMageGreyEncounter:0:1")
                    self.assertEqual(set(graphics["missile"]), {"ray"})
                self.assertEqual(set(graphics), expected_fields)
                self.assertTrue((IMAGE_ROOT / image).is_file())

        generator = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
        for image in expected.values():
            self.assertIn(f'ImagePath::builtin("{image}")', generator)
        self.assertNotIn("NH_academy_stoneGargoyle_icon_large.png", generator)
        self.assertNotIn("NH_academy_obsidianGargoyle_icon_large.png", generator)
        self.assertNotIn("core:stoneGargoyle", config)
        self.assertNotIn("core:obsidianGargoyle", config)

    def test_portrait_frame_ids_preserve_hottraits_reversal(self):
        core = parse_jsonc((ROOT / "config/creatures/tower.json").read_text())
        self.assertEqual(core["ironGolem"]["index"], 32)
        self.assertEqual(core["stoneGolem"]["index"], 33)
        self.assertEqual(core["mage"]["index"], 34)
        self.assertEqual(core["archMage"]["index"], 35)
        self.assertEqual(core["genie"]["index"], 36)
        self.assertEqual(core["masterGenie"]["index"], 37)
        self.assertEqual(core["naga"]["index"], 38)
        self.assertEqual(core["nagaQueen"]["index"], 39)
        self.assertEqual(core["giant"]["index"], 40)
        self.assertEqual(core["titan"]["index"], 41)
        fixture = (ROOT / "client/tests/AcademyBuiltIconRuntimeTest.cpp").read_text(encoding="utf-8")
        self.assertIn('{"ironGolem", "NH_academy_ironGolem_icon_large.png", "NH_academy_ironGolem_portrait_mask.png", 32, 34}', fixture)
        self.assertIn('{"stoneGolem", "NH_academy_stoneGolem_icon_large.png", "NH_academy_stoneGolem_portrait_mask.png", 33, 35}', fixture)
        self.assertIn('{"mage", "NH_academy_mage_icon_large.png", "NH_academy_mage_portrait_mask.png", 34, 36}', fixture)
        self.assertIn('{"archMage", "NH_academy_archMage_icon_large.png", "NH_academy_archMage_portrait_mask.png", 35, 37,', fixture)
        self.assertIn('nullptr, "NH_ArchMageGreyPortrait", 0}', fixture)
        self.assertIn(
            '{"genie", "NH_academy_genie_icon_large.png", "NH_academy_genie_portrait_mask.png", 36, 38}',
            fixture,
        )
        self.assertIn(
            '{"masterGenie", "NH_academy_masterGenie_icon_large.png", "NH_academy_masterGenie_portrait_mask.png", 37, 39}',
            fixture,
        )
        self.assertIn(
            '{"naga", "NH_academy_naga_icon_large.png", "NH_academy_naga_portrait_mask.png", 38, 40}',
            fixture,
        )
        self.assertIn(
            '{"nagaQueen", "NH_academy_nagaQueen_icon_large.png", "NH_academy_nagaQueen_portrait_mask.png", 39, 41}',
            fixture,
        )
        self.assertIn(
            '{"giant", "NH_academy_giant_icon_large.png", "NH_academy_giant_portrait_mask.png", 40, 42}',
            fixture,
        )
        self.assertIn(
            '{"titan", "NH_academy_titan_icon_large.png", "NH_academy_titan_portrait_mask.png", 41, 43}',
            fixture,
        )
        config = (ROOT / "Mods/new-horizons/Content/config/creatures/tower.json").read_text(encoding="utf-8")
        self.assertIn("frame 34 is internal ironGolem ID 32; frame 35 is stoneGolem ID 33", config)


if __name__ == "__main__":
    unittest.main()
