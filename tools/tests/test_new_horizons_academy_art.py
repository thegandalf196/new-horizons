#!/usr/bin/env python3
"""Focused Academy art registration and provenance checks."""

import json
from pathlib import Path
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "Mods/new-horizons/Images"
ACADEMY_SOURCE = ROOT / "assets/new-horizons/academy"
ART_PATCH = ROOT / "Mods/new-horizons/Content/config/factions/academyArt.json"


def read_json(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


class NewHorizonsAcademyArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = read_json("Mods/new-horizons/Content/config/factions/academyArt.json")["core:tower"]
        cls.town = cls.patch["town"]
        cls.structures = cls.town["structures"]

    def test_faction_identity_terrain_and_visual_scope(self):
        self.assertEqual(self.patch["name"], "Academy")
        self.assertEqual(self.patch["nativeTerrain#override"], ["sand"])
        self.assertNotIn("buildings", self.town)
        self.assertNotIn("hallSlots", self.town)
        self.assertNotIn("creatures", self.town)
        self.assertEqual(self.town["buildingsIcons"], "NH_tower_buildings")

        expected = {
            "dwellingLvl4": ("NH_ACADEMY_TBTWDW_4", 511, 75),
            "dwellingLvl5": ("NH_ACADEMY_TBTWDW_3", 613, 95),
            "dwellingUpLvl4": ("NH_ACADEMY_TBTWUP_4", 511, 8),
            "dwellingUpLvl5": ("NH_ACADEMY_TBTWUP_3", 613, 74),
        }
        for name, (resource, x, y) in expected.items():
            with self.subTest(structure=name):
                structure = self.structures[name]
                self.assertEqual((structure["animation"], structure["x"], structure["y"]), (resource, x, y))

        roof = self.structures["academyRoof"]
        self.assertEqual((roof["animation"], roof["x"], roof["y"], roof["z"]),
                         ("NH_ACADEMY_TOWN_ROOF", 665, 255, 4))
        for interaction_field in ("area", "border", "campaignBonus", "builds"):
            self.assertNotIn(interaction_field, roof)

    def test_registered_art_masks_icons_and_map_bodies_resolve(self):
        for name, structure in self.structures.items():
            if name == "academyRoof":
                resource = structure["animation"]
                descriptor = read_json(f"Mods/new-horizons/Images/{resource}.json")
                image_path = descriptor["images"][0]["file"]
            else:
                for field in ("area", "border", "campaignBonus"):
                    with self.subTest(structure=name, field=field):
                        self.assertTrue((IMAGES / structure[field]).is_file())
                descriptor = read_json(f"Mods/new-horizons/Images/{structure['animation']}.json")
                image_path = descriptor["images"][0]["file"]
                with Image.open(IMAGES / structure["area"]) as area, Image.open(IMAGES / structure["border"]) as border:
                    with Image.open(IMAGES / image_path) as art:
                        self.assertEqual(area.size, art.size, name)
                        self.assertEqual(border.size, art.size, name)
                    self.assertGreater(area.getchannel("A").getbbox()[2], 0, name)
                    self.assertGreater(border.getchannel("A").getbbox()[2], 0, name)
            self.assertTrue((IMAGES / image_path).is_file(), name)

        hall = read_json("Mods/new-horizons/Images/NH_tower_buildings.json")["images"]
        self.assertEqual(len(hall), 44)
        self.assertEqual([entry["frame"] for entry in hall], list(range(44)))
        for frame, entry in enumerate(hall):
            source_frame = {33: 34, 34: 33, 40: 41, 41: 40}.get(frame, frame)
            self.assertEqual(entry["file"], f"NH_academy/ui/hall/frame-{source_frame:03d}.png")
            self.assertTrue((IMAGES / entry["file"]).is_file())

        for resource, filename in (
            ("NH_ACADEMY_VILLAGE_BODY", "NH_academy_village_body.png"),
            ("NH_ACADEMY_FORT_BODY", "NH_academy_fort_body.png"),
            ("NH_ACADEMY_CAPITOL_BODY", "NH_academy_capitol_body.png"),
        ):
            descriptor = read_json(f"Mods/new-horizons/Images/{resource}.json")
            with Image.open(IMAGES / descriptor["images"][0]["file"]) as body:
                self.assertEqual(body.size, (192, 192))
            self.assertTrue((IMAGES / filename).is_file())

        for faction_icons in self.town["icons"].values():
            for size in ("large", "small"):
                normal = IMAGES / faction_icons["normal"][size]
                built_fallback = IMAGES / faction_icons["built"][size]
                self.assertTrue(normal.is_file())
                self.assertTrue(faction_icons["built"][size].endswith("_built.png"))
                self.assertTrue(built_fallback.is_file())
                self.assertEqual(built_fallback.read_bytes(), normal.read_bytes())

    def test_siege_uses_direct_images_and_provenance_exceptions_stay_out(self):
        siege_images = sorted(IMAGES.glob("SGTW*.png"))
        self.assertGreater(len(siege_images), 0)
        for image in siege_images:
            self.assertFalse(image.with_suffix(".json").exists(), image.name)
        for original_gate in ("SGTWDRW1.png", "SGTWDRW2.png", "SGTWDRW3.png", "SGTWDRWC.png"):
            self.assertFalse((IMAGES / original_gate).exists(), original_gate)

        excluded = (
            "native/adventure/avctowr0.png",
            "native/adventure/avctowx0.png",
            "native/adventure/avctowz0.png",
            "native/siege/sgtwdrw1.png",
            "native/siege/sgtwdrw2.png",
            "native/siege/sgtwdrw3.png",
            "native/siege/sgtwdrwc.png",
            "native/ui/icons/fort-large-built.png",
            "native/ui/icons/fort-small-built.png",
            "native/ui/icons/village-large-built.png",
            "native/ui/icons/village-small-built.png",
        )
        for relative in excluded:
            self.assertFalse((ACADEMY_SOURCE / relative).exists(), relative)

        validation = read_json("assets/new-horizons/academy/handoff/VALIDATION.json")
        export_validation = read_json("assets/new-horizons/academy/integration/export-validation.json")
        self.assertFalse(validation["VCMITested"])
        self.assertFalse(export_validation["inGameTested"])
        self.assertTrue((ACADEMY_SOURCE / "README.md").is_file())


if __name__ == "__main__":
    unittest.main()
