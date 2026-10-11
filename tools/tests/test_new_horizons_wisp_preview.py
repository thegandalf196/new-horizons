"""Focused integrity checks for the private Wisp art-preview overlay."""

import hashlib
import json
from pathlib import Path
import sys
import unittest
import tempfile
from io import BytesIO
from unittest.mock import patch
from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_new_horizons_wisp_preview as wisp_preview  # noqa: E402
if __package__:
    from .test_new_horizons_content import load
else:
    from test_new_horizons_content import load


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
        # Exercise the actual pinned handoff reader/copier with a complete,
        # synthetic handoff, not the removed private artist submission.
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            files = {}
            image = BytesIO()
            Image.new("RGBA", (32, 32), (50, 100, 150, 255)).save(image, format="PNG")
            def add(name, data):
                destination = package / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
                files[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
            for name, descriptor, assets in wisp_preview.SPRITE_SPECS:
                groups = 32 if name in {"Wisp.json", "WispUpgrade.json"} else 1
                sequences = [{"group": group, "frames": ["fixture.png"]} for group in range(groups)]
                add(descriptor, json.dumps({"basepath": name[:-5] + "/", "sequences": sequences}).encode())
                add(assets + "/fixture.png", image.getvalue())
            for name, descriptor, assets, basepath in wisp_preview.PROJECTILE_SPECS:
                frames = [f"bolt-phase-00-angle-{angle}.png" for angle in range(9)]
                add(descriptor, json.dumps({"sequences": [{"group": group, "frames": frames} for group in range(4)]}).encode())
                for frame in frames:
                    add(assets + "/" + frame, image.getvalue())
            for icon in ("base/art/Wisp/icons/wisp-icon-32.png", "base/art/Wisp/icons/wisp-icon-58x64.png", "upgraded/icons/icon-32x32.png", "upgraded/icons/icon-58x64.png"):
                add(icon, image.getvalue())
            manifest = json.dumps({"files": files}).encode()
            (package / "HANDOFF_MANIFEST.json").write_bytes(manifest)
            # This historical preview builder expects its original overlay
            # patch shape. Current shipped gameplay has since evolved; test
            # transformation of an explicit synthetic input, not that roster.
            mod_source = package / "synthetic-module"
            seed = {
                "mod.json": {"description": "Fixture", "settings": {
                    "creatures": {"newHorizonsCategories": {"creatures": {}, "growthLines": {
                        "core:pixie": {}, "core:psychicElemental": {}}}},
                    "heroes": {"newHorizonsCapabilities": {"leadership": {"creatureRequirements": {}}}}}},
                "Content/config/creatures/conflux.json": {},
                "Content/config/factions/confluxCreatureRanks.json": {"core:conflux": {"town": {
                    "hallSlots": {"modify@4": {}, "modify@5": {}},
                    "structures": {"dwellingLvl8": {}, "dwellingUpLvl1": {}},
                    "buildings": {"dwellingLvl8": {}, "dwellingUpLvl1": {}, "horde1": {}}}}},
            }
            for name, value in seed.items():
                path = mod_source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value))
            with patch.object(wisp_preview, "HANDOFF_MANIFEST_SHA256", hashlib.sha256(manifest).hexdigest()):
                cls.outputs, cls.metadata = wisp_preview.build_overlay(package, mod_source)
                (package / "base/art/Wisp/fixture.png").write_bytes(b"tampered")
                with unittest.TestCase().assertRaisesRegex(ValueError, "hash/size mismatch"):
                    wisp_preview.build_overlay(package, mod_source)
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

    def test_all_battle_groups_generate_selection_overlays(self):
        for descriptor_name in ("Wisp.json", "WispUpgrade.json"):
            descriptor = self._json_output(
                self.outputs, f"Mods/new-horizons/Content/sprites/{descriptor_name}"
            )
            self.assertEqual(len(descriptor["sequences"]), 32)
            self.assertTrue(
                all(sequence.get("generateOverlay") == 1 for sequence in descriptor["sequences"]),
                descriptor_name,
            )

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

    def test_preview_and_shipping_attacks_use_magic_arrow_sound(self):
        expected = load('config/spells/offensive.json')['magicArrow']['sounds']['cast'] + '.wav'
        self.assertEqual(expected, 'MAGICBLT.wav')
        generated = self._json_output(
            self.outputs, 'Mods/new-horizons/Content/config/creatures/conflux.json')
        for key in ('core:psychicElemental', 'core:magicElemental'):
            with self.subTest(preview=key):
                self.assertEqual(generated[key]['sound']['attack'], expected)
                self.assertEqual(generated[key]['sound']['shoot'], expected)
        shipped = load('Mods/new-horizons/Content/config/creatures/conflux.json')
        for key in ('wisp', 'wispUpgrade'):
            with self.subTest(shipped=key):
                self.assertEqual(shipped[key]['sound']['attack'], expected)
                self.assertNotIn('shooter', shipped[key]['abilities'])
                self.assertNotIn('shoot', shipped[key]['sound'])

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
