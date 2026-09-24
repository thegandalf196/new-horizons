#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep Metamagic's canonical and generated image bindings in sync."""

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
SLOTS = ("small", "medium", "large", "scenarioBonus")
HERO_PATCH = ROOT / "Mods/new-horizons/Content/config/heroes/halon.json"


def expected_images(rank):
    return {
        slot: f"NH_metamagic_prism_{rank}_{slot}.png"
        for slot in SLOTS
    }


class MetamagicArtBindingsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canonical = json.loads(
            (ROOT / "config/newHorizonsSkills.json").read_text(encoding="utf-8")
        )
        cls.generated = json.loads(
            (ROOT / "Mods/new-horizons/mod.json").read_text(encoding="utf-8")
        )
        cls.hero_patches = json.loads(HERO_PATCH.read_text(encoding="utf-8"))

    def test_metamagic_uses_the_rank_specific_prism_family(self):
        all_names = []
        for rank in RANKS:
            with self.subTest(rank=rank):
                expected = expected_images(rank)
                self.assertEqual(self.canonical["metamagic"][rank]["images"], expected)
                self.assertEqual(self.generated["skills"]["metamagic"][rank]["images"], expected)
                all_names.extend(expected.values())
        self.assertEqual(len(all_names), 12)
        self.assertEqual(len(set(all_names)), 12)

    def test_halon_and_serena_specialties_use_the_bound_basic_icons(self):
        basic_images = self.canonical["metamagic"]["basic"]["images"]
        self.assertIn("config/heroes/halon.json", self.generated["heroes"])
        for hero in ("core:halon", "core:serena"):
            with self.subTest(hero=hero):
                images = self.hero_patches[hero]["images"]
                self.assertEqual(images["specialtySmall"], basic_images["small"])
                self.assertEqual(images["specialtyLarge"], basic_images["large"])
                self.assertTrue(
                    (ROOT / "Mods/new-horizons/Images" / images["specialtySmall"]).is_file()
                )
                self.assertTrue(
                    (ROOT / "Mods/new-horizons/Images" / images["specialtyLarge"]).is_file()
                )


if __name__ == "__main__":
    unittest.main()
