"""Offline validation of supplied Mage Guild assets and NH integration."""
import importlib.util
from io import BytesIO
import json
from pathlib import Path
from pathlib import PurePosixPath
import re
import unittest
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "Mods/new-horizons/Content"
SOURCE = ROOT / "assets/new-horizons/Mage Guilds"
PACKAGES = {
    "castle": ("castle-mage-guild-level5", "castle-mage-guild.json", "HALLCSTL", {4}),
    "fortress": ("fortress-mage-guild-v9", "fortress-mage-guild.json", "HALLFORT", {0, 1, 2, 3, 4}),
    "stronghold": ("stronghold-mage-guild-ridge-swap", "stronghold-ridge-swap.json", "HALLSTRN", {0, 1, 2, 3, 4, 23}),
}


def importer():
    spec = importlib.util.spec_from_file_location("guild_import", ROOT / "tools/import_new_horizons_mage_guilds.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def package_config(archive, filename, module):
    root = module._content_root(archive)
    matches = []
    for name in archive.namelist():
        path = PurePosixPath(name)
        if path.parts[:len(root)] != root:
            continue
        relative = path.parts[len(root):]
        if (len(relative) == 2 and relative[0].casefold() == "config"
                and relative[1].casefold() == filename.casefold()):
            matches.append(name)
    if len(matches) != 1:
        raise AssertionError(f"Expected one {filename} in package Content/config; found {matches}")
    return json.loads(archive.read(matches[0]))


def load(path):
    return json.loads(re.sub(r"(?m)^\s*//.*$", "", path.read_text()))


class MageGuildAssetsTest(unittest.TestCase):
    def test_raw_asset_bytes_match_supplied_archives_and_pngs_decode(self):
        from PIL import Image
        module = importer()
        counts = {".png": 0, ".def": 0, ".pcx": 0, ".bmp": 0}
        for package, _, _, _ in PACKAGES.values():
            with ZipFile(SOURCE / (package + ".zip")) as archive:
                for relative, data in module._runtime_assets(archive):
                    extension = relative.suffix.casefold()
                    if extension not in counts:
                        continue
                    target = CONTENT / relative
                    self.assertEqual(target.read_bytes(), data, relative.as_posix())
                    counts[extension] += 1
                    if extension == ".png":
                        with Image.open(BytesIO(data)) as image:
                            image.verify()
        self.assertEqual(counts, {".png": 41, ".def": 5, ".pcx": 5, ".bmp": 10})

    def test_v9_archive_root_and_directory_case_are_normalized(self):
        module = importer()
        archive_path = SOURCE / "fortress-mage-guild-v9.zip"
        with ZipFile(archive_path) as archive:
            self.assertEqual(module._content_root(archive), ("fortress-mage-guild", "content"))
            self.assertTrue(any(name.startswith("fortress-mage-guild/content/Data/") for name in archive.namelist()))
            self.assertTrue(any(name.startswith("fortress-mage-guild/content/Sprites/") for name in archive.namelist()))

        for name in ("TBFRMAGE.def", "TBFRMAG2.def", "TBFRMAG3.def", "TBFRMAG4.def", "TBFRMAG5.def"):
            self.assertTrue((CONTENT / "sprites" / name).is_file(), name)
        for name in ("BoFMage1.pcx", "TOFMAG1A.bmp", "TZFMAG1A.bmp"):
            self.assertTrue((CONTENT / "data" / name).is_file(), name)

    def test_fortress_hall_cards_are_native_size_and_imported_unchanged(self):
        from PIL import Image
        module = importer()
        with ZipFile(SOURCE / "fortress-mage-guild-v9.zip") as archive:
            for relative, data in module._runtime_assets(archive):
                if relative.name not in {f"fortress-mage-guild-hall-{level}.png" for level in range(1, 6)}:
                    continue
                self.assertEqual((CONTENT / relative).read_bytes(), data)
                with Image.open(BytesIO(data)) as image:
                    self.assertEqual(image.size, (150, 70), relative.name)
                    self.assertEqual(image.mode, "RGB", relative.name)

    def test_reproducible_import(self):
        module = importer()
        assets = list(module.assets())
        self.assertEqual(len(assets), 71)
        for path, data in assets:
            self.assertEqual((CONTENT / path).read_bytes(), data, str(path))

    def test_all_animation_frames_resolve_with_preserved_counts(self):
        sprites = CONTENT / "sprites"
        expected = {"TBCSMAG5": 11, "SVAHSW": 1}
        expected.update({f"SMAGSW{level}": 1 for level in range(1, 6)})
        for name, count in expected.items():
            descriptor = load(sprites / (name + ".json"))
            self.assertEqual(len(descriptor["sequences"]), 1)
            sequence = descriptor["sequences"][0]
            self.assertEqual(sequence["group"], 0)
            self.assertEqual(len(sequence["frames"]), count)
            for frame in sequence["frames"]:
                self.assertTrue((sprites / (descriptor.get("basepath", "") + frame)).is_file())

    def test_fortress_v9_binds_all_five_native_def_levels_and_removes_old_descriptors(self):
        overlay = load(CONTENT / "config/factions/universalMageGuilds.json")
        structures = overlay["core:fortress"]["town"]["structures"]
        supplied = {
            "mageGuild1": ("TBFRMAGE.def", 200, -1, "BoFMage1.pcx", "TOFMAG1A.bmp", "TZFMAG1A.bmp"),
            "mageGuild2": ("TBFRMAG2.def", 177, -1, "BoFMage2.pcx", "TOFMAG2A.bmp", "TZFMAG2A.bmp"),
            "mageGuild3": ("TBFRMAG3.def", 135, -1, "BoFMage3.pcx", "TOFMAG3A.bmp", "TZFMAG3A.bmp"),
            "mageGuild4": ("TBFRMAG4.def", 92, 1, "BoFMage4.pcx", "TOFMAG4A.bmp", "TZFMAG4A.bmp"),
            "mageGuild5": ("TBFRMAG5.def", 79, 1, "BoFMage5.pcx", "TOFMAG5A.bmp", "TZFMAG5A.bmp"),
        }
        self.assertEqual(set(structures), set(supplied))
        for building, (animation, y, z, campaign, border, area) in supplied.items():
            self.assertEqual(structures[building], {
                "animation": animation,
                "x": 0,
                "y": y,
                "z": z,
                "campaignBonus": campaign,
                "border": border,
                "area": area,
            })
            self.assertTrue((CONTENT / "sprites" / animation).is_file())
            self.assertTrue((CONTENT / "data" / campaign).is_file())
            self.assertTrue((CONTENT / "data" / border).is_file())
            self.assertTrue((CONTENT / "data" / area).is_file())

        obsolete = [CONTENT / "sprites" / f"TBFRMAG{level}.json" for level in (4, 5)]
        obsolete.extend(CONTENT / "sprites" / f"TBFRMAG{level}" for level in (4, 5))
        obsolete.extend(CONTENT / "sprites" / "HALLFORT" / f"mage-guild-{level}.png" for level in (4, 5))
        for path in obsolete:
            self.assertFalse(path.exists(), str(path))

    def test_hall_overrides_only_replace_intended_frames(self):
        for _, _, hall, expected in PACKAGES.values():
            descriptor = load(CONTENT / "sprites" / (hall + ".json"))
            self.assertNotIn("sequences", descriptor)
            self.assertEqual({item["frame"] for item in descriptor["images"]}, expected)
            for item in descriptor["images"]:
                self.assertEqual(item["group"], 0)
                self.assertTrue((CONTENT / "sprites" / (descriptor.get("basepath", "") + item["file"])).is_file())

    def test_structure_placement_matches_packages_without_changing_nh_rules(self):
        overlay = load(CONTENT / "config/factions/universalMageGuilds.json")
        module = importer()
        for faction, (package, config, hall, _) in PACKAGES.items():
            with ZipFile(SOURCE / (package + ".zip")) as archive:
                supplied = package_config(archive, config, module)["core:" + faction]["town"]
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

        fortress = overlay["core:fortress"]["town"]
        self.assertEqual(fortress["hallSlots"][1][1], [f"mageGuild{level}" for level in range(1, 6)])
        self.assertEqual(fortress["guildSpellPositions"], [
            [{"x": x, "y": 445} for x in (222, 312, 402, 520, 610, 700)],
            [{"x": 48, "y": y} for y in (53, 147, 241, 335, 429)],
            [{"x": x, "y": y} for x, y in ((570, 82), (672, 82), (570, 157), (672, 157))],
            [{"x": 183, "y": y} for y in (42, 148, 253)],
            [{"x": x, "y": 325} for x in (491, 591)],
        ])


if __name__ == "__main__":
    unittest.main()
