"""Offline validation of supplied Mage Guild assets and NH integration."""
import importlib.util
import json
from pathlib import Path
import re
import unittest
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "Mods/new-horizons/Content"
SOURCE = ROOT / "assets/new-horizons/Mage Guilds"
PACKAGES = {
    "castle": ("castle-mage-guild-level5", "castle-mage-guild.json", "HALLCSTL", {4}),
    "fortress": ("fortress-mage-guild-v8", "fortress-mage-guild.json", "HALLFORT", {3, 4}),
    "stronghold": ("stronghold-mage-guild-ridge-swap", "stronghold-ridge-swap.json", "HALLSTRN", {0, 1, 2, 3, 4, 23}),
}


def load(path):
    return json.loads(re.sub(r"(?m)^\s*//.*$", "", path.read_text()))


class MageGuildAssetsTest(unittest.TestCase):
    def test_png_bytes_match_supplied_archives_and_decode(self):
        from PIL import Image
        count = 0
        for package, _, _, _ in PACKAGES.values():
            with ZipFile(SOURCE / (package + ".zip")) as archive:
                for name in archive.namelist():
                    if not name.lower().endswith(".png"):
                        continue
                    target = CONTENT / name.split("/Content/", 1)[1]
                    self.assertEqual(target.read_bytes(), archive.read(name), name)
                    with Image.open(target) as image:
                        image.verify()
                    count += 1
        self.assertEqual(count, 80)

    def test_reproducible_import(self):
        spec = importlib.util.spec_from_file_location("guild_import", ROOT / "tools/import_new_horizons_mage_guilds.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        assets = list(module.assets())
        self.assertEqual(len(assets), 92)
        for path, data in assets:
            self.assertEqual((CONTENT / path).read_bytes(), data, str(path))

    def test_all_animation_frames_resolve_with_preserved_counts(self):
        sprites = CONTENT / "sprites"
        expected = {"TBCSMAG5": 11, "TBFRMAG4": 21, "TBFRMAG5": 21, "SVAHSW": 1}
        expected.update({f"SMAGSW{level}": 1 for level in range(1, 6)})
        for name, count in expected.items():
            descriptor = load(sprites / (name + ".json"))
            self.assertEqual(len(descriptor["sequences"]), 1)
            sequence = descriptor["sequences"][0]
            self.assertEqual(sequence["group"], 0)
            self.assertEqual(len(sequence["frames"]), count)
            for frame in sequence["frames"]:
                self.assertTrue((sprites / (descriptor.get("basepath", "") + frame)).is_file())

    def test_hall_overrides_only_replace_intended_frames(self):
        for _, _, hall, expected in PACKAGES.values():
            descriptor = load(CONTENT / "sprites" / (hall + ".json"))
            self.assertNotIn("sequences", descriptor)
            self.assertEqual({item["frame"] for item in descriptor["images"]}, expected)
            for item in descriptor["images"]:
                self.assertEqual(item["group"], 0)
                self.assertTrue((CONTENT / "sprites" / (descriptor["basepath"] + item["file"])).is_file())

    def test_structure_placement_matches_packages_without_changing_nh_rules(self):
        overlay = load(CONTENT / "config/factions/universalMageGuilds.json")
        for faction, (package, config, hall, _) in PACKAGES.items():
            with ZipFile(SOURCE / (package + ".zip")) as archive:
                supplied = json.loads(archive.read(package + "/Content/config/" + config))["core:" + faction]["town"]
            actual = overlay["core:" + faction]["town"]
            self.assertEqual(actual["structures"], supplied["structures"])
            self.assertEqual(actual["buildingsIcons"], hall)
            self.assertEqual(actual["mageGuild"], 5)
            self.assertEqual([len(row) for row in actual["guildSpellPositions"]], [6, 5, 4, 3, 2])
            self.assertIn([f"mageGuild{level}" for level in range(1, 6)], actual["hallSlots"][1])
            for level, gold, rare in ((4, 5000, 5), (5, 10000, 10)):
                building = actual["buildings"][f"mageGuild{level}"]
                self.assertEqual(building["cost#override"], dict(gold=gold, mercury=rare, sulfur=rare, crystal=rare, gems=rare))
                if faction != "castle" or level == 5:
                    self.assertEqual(building["upgrades"], f"mageGuild{level - 1}")


if __name__ == "__main__":
    unittest.main()
