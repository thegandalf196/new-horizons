#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Selected shipping casting-art contract, not rendered acceptance or rights proof."""
from io import BytesIO
import json
from pathlib import Path, PurePosixPath
import unittest

from PIL import Image
if __package__:
    from .nhart_test_resources import read_resource, resource_names
else:
    from nhart_test_resources import read_resource, resource_names


ROOT = Path(__file__).resolve().parents[2]
HERO_KEYS = {
    "CH00", "CH01", "CH010", "CH012", "CH013", "CH014", "CH015",
    "CH02", "CH03", "CH04", "CH05", "CH06", "CH07", "CH08", "CH09",
    "CH11", "CH16", "CH17",
}
SCHOOLS = {"light", "nature", "sorcery", "havoc", "chaos", "shadow"}


class NewHorizonsCastingArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(read_resource("CONFIG/newHorizonsMagicAssets.json"))

    def references(self):
        return [frame for schools in self.config["castingGlows"].values()
                for definition in schools.values() for frame in definition["frames"]]

    def test_casting_only_native_keys_and_frame_contract(self):
        self.assertEqual(set(self.config), {"castingGlows", "guildBooks"})
        self.assertEqual(set(self.config["castingGlows"]), HERO_KEYS)
        for hero, schools in self.config["castingGlows"].items():
            with self.subTest(hero=hero):
                self.assertEqual(set(schools), SCHOOLS)
            for school, definition in schools.items():
                with self.subTest(hero=hero, school=school):
                    self.assertEqual(set(definition), {"frames", "dimensions"})
                    self.assertEqual(definition["dimensions"], [150, 175])
                    self.assertEqual(definition["frames"], [
                        f"NH_magic_assets/casting/{hero}/{school}/frame_{index:02}.png"
                        for index in range(8)])
        references = self.references()
        self.assertEqual(len(references), 18 * 6 * 8)
        self.assertEqual(len(set(references)), len(references))

    def test_references_are_safe_and_exhaust_public_png_inventory(self):
        references = self.references()
        for reference in references:
            with self.subTest(reference=reference):
                path = PurePosixPath(reference)
                self.assertFalse(path.is_absolute())
                self.assertNotIn("..", path.parts)
                self.assertNotIn("\\", reference)
                self.assertNotIn(":", reference)
                self.assertEqual(path.parts[:2], ("NH_magic_assets", "casting"))
                self.assertIn("SPRITES/" + reference, resource_names())
        # Guild composites are independently selected runtime resources now;
        # the casting subtree must still contain exactly its bound frame set.
        files = {name.removeprefix("SPRITES/") for name in resource_names("SPRITES/NH_magic_assets/casting/")}
        self.assertEqual(files, set(references))
        self.assertTrue(all(PurePosixPath(name).suffix == ".png" for name in files))

    def test_all_frames_are_native_transparent_rgba_png(self):
        for reference in self.references():
            with self.subTest(reference=reference), Image.open(BytesIO(read_resource("SPRITES/" + reference))) as image:
                self.assertEqual(image.format, "PNG")
                self.assertFalse(getattr(image, "is_animated", False))
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(image.size, (150, 175))
                self.assertEqual(image.getchannel("A").getextrema()[0], 0)
                # Completely transparent fade-out frames are intentional.

    def test_selected_guild_composites_have_explicit_bindings(self):
        factions = {"castle", "rampart", "tower", "inferno", "necropolis",
                    "dungeon", "stronghold", "fortress", "conflux"}
        self.assertEqual(set(self.config["guildBooks"]), factions)
        expected = set()
        for faction, definition in self.config["guildBooks"].items():
            with self.subTest(faction=faction):
                self.assertEqual(set(definition), {"image", "position"})
                self.assertEqual(definition["image"], f"NH_magic_assets/guild/{faction}.png")
                self.assertEqual(definition["position"], [378, 344])
                name = "SPRITES/" + definition["image"]
                expected.add(name)
                with Image.open(BytesIO(read_resource(name))) as image:
                    self.assertEqual(image.format, "PNG")
                    self.assertFalse(getattr(image, "is_animated", False))
                    image.load()
        self.assertEqual(resource_names("SPRITES/NH_magic_assets/guild/"), expected)


if __name__ == "__main__":
    unittest.main()
