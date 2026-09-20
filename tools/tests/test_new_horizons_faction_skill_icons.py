#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused audit for the nine New Horizons faction-skill icon families."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config/newHorizonsSkills.json"
IMAGES = ROOT / "Mods/new-horizons/Images"
PROVENANCE = ROOT / "assets/new-horizons/art-source/skill-icons"
RANKS = ("basic", "advanced", "expert")
SIZES = {
    "small": (32, 32),
    "medium": (44, 44),
    "large": (82, 93),
    "scenarioBonus": (58, 64),
}
SKILLS = (
    "divineMandate",
    "sylvanLuck",
    "metamagic",
    "demonicGating",
    "necromancy",
    "shroudOfMalassa",
    "bloodrage",
    "bulwarkOfTheMire",
    "elementalRebirth",
)
GENERATED_SKILLS = tuple(skill for skill in SKILLS if skill != "necromancy")
NECROMANCY_FRAMES = {"basic": 39, "advanced": 40, "expert": 41}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class NewHorizonsFactionSkillIconAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.skills = json.loads(CONFIG.read_text(encoding="utf-8"))
        cls.manifest = json.loads(
            (PROVENANCE / "faction_skill_icon_manifest.json").read_text(encoding="utf-8")
        )

    def test_manifest_covers_exactly_the_nine_faction_families(self):
        self.assertEqual(set(self.manifest["skills"]), set(SKILLS))
        self.assertEqual(tuple(self.manifest["ranks"]), RANKS)
        self.assertEqual(self.manifest["sizes"], {key: list(value) for key, value in SIZES.items()})
        for skill in SKILLS:
            source = self.manifest["skills"][skill]["source"]
            self.assertTrue(source)
            self.assertNotIn("NH_lightMagic", source)

    def test_config_points_each_rank_to_its_own_family(self):
        for skill in SKILLS:
            with self.subTest(skill=skill):
                for rank in RANKS:
                    images = self.skills[skill][rank]["images"]
                    if skill == "necromancy":
                        frame = NECROMANCY_FRAMES[rank]
                        expected = {
                            "small": f"SECSK32:0:{frame}",
                            "medium": f"SECSKILL:0:{frame}",
                            "large": f"SECSK82:0:{frame}",
                            "scenarioBonus": f"SECSKILL:0:{frame}",
                        }
                    else:
                        expected = {
                            size: f"NH_{skill}_{rank}_{size}.png" for size in SIZES
                        }
                    self.assertEqual(images, expected)
                    self.assertNotIn("NH_lightMagic_", json.dumps(images))

    def test_every_family_has_native_slot_dimensions_and_unique_rank_art(self):
        family_medium_hashes = set()
        for skill in GENERATED_SKILLS:
            rank_hashes = set()
            for rank in RANKS:
                for size_name, dimensions in SIZES.items():
                    path = IMAGES / f"NH_{skill}_{rank}_{size_name}.png"
                    self.assertTrue(path.is_file(), path)
                    with Image.open(path) as image:
                        self.assertEqual(image.size, dimensions, path)
                        self.assertEqual(image.mode, "RGBA", path)
                medium = IMAGES / f"NH_{skill}_{rank}_medium.png"
                rank_hashes.add(digest(medium))
                family_medium_hashes.add(digest(medium))
            self.assertEqual(len(rank_hashes), len(RANKS), skill)
        self.assertEqual(len(family_medium_hashes), len(GENERATED_SKILLS) * len(RANKS))

    def test_necromancy_uses_purchaser_supplied_classic_frames(self):
        for rank, frame in NECROMANCY_FRAMES.items():
            images = self.skills["necromancy"][rank]["images"]
            self.assertEqual(images["small"], f"SECSK32:0:{frame}")
            self.assertEqual(images["medium"], f"SECSKILL:0:{frame}")
            self.assertEqual(images["large"], f"SECSK82:0:{frame}")
            self.assertEqual(images["scenarioBonus"], f"SECSKILL:0:{frame}")
        self.assertFalse(any(IMAGES.glob("NH_necromancy_*.png")))
        self.assertFalse((PROVENANCE / "classic-necromancy").exists())


if __name__ == "__main__":
    unittest.main()
