"""Focused integrity checks for the private Wisp art-preview overlay."""

import hashlib
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_new_horizons_wisp_preview as wisp_preview  # noqa: E402


class WispPreviewOverlayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_files = (
            ROOT / "Mods/new-horizons/mod.json",
            ROOT / "Mods/new-horizons/Content/config/creatures/conflux.json",
            ROOT / "Mods/new-horizons/Content/config/factions/confluxCreatureRanks.json",
        )
        cls.source_hashes_before = {
            path: hashlib.sha256(path.read_bytes()).hexdigest() for path in cls.source_files
        }
        cls.outputs, cls.metadata = wisp_preview.build_overlay()
        cls.source_hashes_after = {
            path: hashlib.sha256(path.read_bytes()).hexdigest() for path in cls.source_files
        }

    @staticmethod
    def _json_output(outputs, relative):
        return json.loads(outputs[relative])

    def test_descriptors_resolve_all_copied_art_and_projectile_is_group_zero(self):
        descriptor_paths = sorted(
            path for path in self.outputs if path.startswith("Mods/new-horizons/Content/sprites/")
        )
        self.assertEqual(len(descriptor_paths), 6)
        for descriptor_path in descriptor_paths:
            descriptor = self._json_output(self.outputs, descriptor_path)
            basepath = descriptor["basepath"]
            for sequence in descriptor["sequences"]:
                for frame in sequence["frames"]:
                    image_path = f"Mods/new-horizons/Images/{basepath}{frame}"
                    self.assertIn(image_path, self.outputs, image_path)

        for descriptor_path in (
            "Mods/new-horizons/Content/sprites/WispProjectile.json",
            "Mods/new-horizons/Content/sprites/WispUpgradeProjectile.json",
        ):
            descriptor = self._json_output(self.outputs, descriptor_path)
            self.assertEqual([sequence["group"] for sequence in descriptor["sequences"]], [0])
            self.assertEqual(len(descriptor["sequences"][0]["frames"]), 9)

        self.assertEqual(self.metadata["projectiles"]["runtimeGroups"], [0])
        self.assertEqual(self.metadata["projectiles"]["framesPerForm"], 9)

    def test_preview_roster_and_borrowed_profile_are_explicit_and_isolated(self):
        creature_config = self._json_output(
            self.outputs, "Mods/new-horizons/Content/config/creatures/conflux.json"
        )
        faction_config = self._json_output(
            self.outputs, "Mods/new-horizons/Content/config/factions/confluxCreatureRanks.json"
        )
        mod = self._json_output(self.outputs, "Mods/new-horizons/mod.json")
        town = faction_config["core:conflux"]["town"]
        self.assertEqual(town["creatures"], {"modify@1": ["pixie", "sprite"]})
        self.assertEqual(
            town["hallSlots"]["modify@4"]["modify@1"],
            ["dwellingLvl1", "dwellingUpLvl1"],
        )
        self.assertNotIn("modify@5", town["hallSlots"])
        self.assertNotIn("dwellingLvl8", town["structures"])
        self.assertNotIn("dwellingLvl8", town["buildings"])
        self.assertNotIn("dwellingUpLvl1", town["structures"])
        self.assertNotIn("dwellingUpLvl1", town["buildings"])
        self.assertNotIn("horde1", town["buildings"])

        base = creature_config["core:psychicElemental"]
        upgraded = creature_config["core:magicElemental"]
        self.assertEqual(creature_config["core:pixie"]["upgrades"], ["sprite"])
        self.assertEqual(base["name"]["singular"], "Wisp (Preview)")
        self.assertEqual(upgraded["name"]["singular"], "Wisp Upgrade (Preview)")
        self.assertEqual(base["upgrades"], ["magicElemental"])
        self.assertEqual(base["abilities#override"], {"shooter": {"type": "SHOOTER"}})
        self.assertEqual(upgraded["abilities#override"], {"shooter": {"type": "SHOOTER"}})
        self.assertEqual(base["shots"], 8)
        self.assertEqual(upgraded["shots"], 8)
        self.assertFalse(base["doubleWide"])
        self.assertEqual(base["graphics"]["mapAttackFromLeft"], "WispMap.def:0:0")
        self.assertEqual(upgraded["graphics"]["mapAttackFromRight"], "WispUpgradeMap.def:0:0")
        self.assertEqual(base["graphics"]["missile"]["projectile"], "WispProjectile.def")
        self.assertEqual(upgraded["graphics"]["missile"]["projectile"], "WispUpgradeProjectile.def")
        self.assertEqual(base["graphics"]["missile"]["offset"]["middleX"], 31)
        self.assertEqual(base["graphics"]["missile"]["offset"]["middleY"], -29)

        settings = mod["settings"]
        categories = settings["creatures"]["newHorizonsCategories"]
        self.assertEqual(categories["creatures"]["core:psychicElemental"], "core")
        self.assertEqual(categories["creatures"]["core:magicElemental"], "core")
        self.assertEqual(
            categories["growthLines"]["core:psychicElemental"]["members"],
            ["core:psychicElemental", "core:magicElemental"],
        )
        requirements = settings["heroes"]["newHorizonsCapabilities"]["leadership"]["creatureRequirements"]
        self.assertEqual(requirements["core:psychicElemental"], 50)
        self.assertEqual(requirements["core:magicElemental"], 60)
        self.assertIn("TEMPORARY PRIVATE ART PREVIEW ONLY", mod["description"])

    def test_builder_is_read_only_against_the_normal_module(self):
        self.assertEqual(self.source_hashes_before, self.source_hashes_after)
        self.assertEqual(
            self.metadata["roster"]["coreLine1"], ["core:pixie", "core:sprite"]
        )
        self.assertEqual(
            self.metadata["roster"]["coreLine2PreviewAliases"],
            ["core:psychicElemental", "core:magicElemental"],
        )
        self.assertIn("not final Wisp rules", self.metadata["status"])


if __name__ == "__main__":
    unittest.main()
