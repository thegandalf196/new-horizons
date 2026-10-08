#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Shipped standalone casting-art contract, not rendered acceptance or rights proof."""
import json
from pathlib import Path, PurePosixPath
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "Mods/new-horizons/Images"
FAMILY = IMAGES / "NH_magic_assets"
CASTING = FAMILY / "casting"
HERO_KEYS = {
    "CH00", "CH01", "CH010", "CH012", "CH013", "CH014", "CH015",
    "CH02", "CH03", "CH04", "CH05", "CH06", "CH07", "CH08", "CH09",
    "CH11", "CH16", "CH17",
}
SCHOOLS = {"light", "nature", "sorcery", "havoc", "chaos", "shadow"}


class NewHorizonsCastingArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads((ROOT / "config/newHorizonsMagicAssets.json").read_text())

    def references(self):
        return [frame for schools in self.config["castingGlows"].values()
                for definition in schools.values() for frame in definition["frames"]]

    def test_casting_only_native_keys_and_frame_contract(self):
        self.assertEqual(set(self.config), {"castingGlows"})
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
                target = IMAGES / path
                self.assertTrue(target.is_file())
                self.assertTrue(target.resolve().is_relative_to(CASTING.resolve()))
                for ancestor in [target, *target.parents]:
                    if ancestor == ROOT:
                        break
                    self.assertFalse(ancestor.is_symlink(), str(ancestor))
        entries = list(FAMILY.rglob("*"))
        self.assertTrue(entries)
        self.assertFalse(any(path.is_symlink() for path in entries))
        files = {path.relative_to(IMAGES).as_posix() for path in entries if path.is_file()}
        # No guild clean plates/composites, base hero frames, DEFs, or unbound
        # files may travel with this specifically authorized standalone set.
        self.assertEqual(files, set(references))
        self.assertTrue(all(PurePosixPath(name).suffix == ".png" for name in files))

    def test_all_frames_are_native_transparent_rgba_png(self):
        for reference in self.references():
            with self.subTest(reference=reference), Image.open(IMAGES / reference) as image:
                self.assertEqual(image.format, "PNG")
                self.assertFalse(getattr(image, "is_animated", False))
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(image.size, (150, 175))
                self.assertEqual(image.getchannel("A").getextrema()[0], 0)
                # Completely transparent fade-out frames are intentional.


if __name__ == "__main__":
    unittest.main()
