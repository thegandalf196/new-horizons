"""Offline validation of supplied Mage Guild assets and NH integration."""
import importlib.util
from io import BytesIO
import json
from pathlib import Path
from pathlib import PurePosixPath
import re
import unittest
import tempfile
from zipfile import ZipFile
from tools.tests.nhart_test_resources import ArtPath, open_image

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "Mods/new-horizons/Content"
SPRITES = ArtPath("SPRITES")
DATA = ArtPath("DATA")
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
    @staticmethod
    def synthetic_packages(module, source):
        source.mkdir()
        for index, package in enumerate(module.PACKAGES):
            with ZipFile(source / (package + ".zip"), "w") as archive:
                archive.writestr(f"guild/Content/Sprites/fixture-{index}.bin", bytes([index + 1]) * 20)
        with ZipFile(source / (module.FORTRESS_V8 + ".zip"), "w") as archive:
            for relative in module._expected_obsolete_fortress_paths():
                payload = b"{}\n" if relative.suffix == ".json" else b"synthetic obsolete"
                archive.writestr("guild/Content/" + relative.as_posix(), payload)

    def test_import_requires_explicit_external_roots_before_read_or_write(self):
        module = importer()
        with self.assertRaisesRegex(ValueError, "Explicit external"):
            module.import_assets()
        with self.assertRaisesRegex(ValueError, "outside the checkout"):
            list(module.assets(ROOT))
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "outside the checkout"):
                module.import_assets(Path(temporary), CONTENT)

    def test_synthetic_external_import_is_idempotent_and_removes_only_known_bytes(self):
        module = importer()
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / "inputs", Path(temporary) / "output"
            self.synthetic_packages(module, source)
            obsolete = output / "sprites/TBFRMAG4.json"
            obsolete.parent.mkdir(parents=True)
            obsolete.write_bytes(b"{}\n")
            self.assertEqual(module.import_assets(source, output), (3, 1))
            self.assertFalse(obsolete.exists())
            self.assertEqual(module.import_assets(source, output), (3, 0))
            for index in range(3):
                self.assertEqual((output / f"sprites/fixture-{index}.bin").read_bytes(), bytes([index + 1]) * 20)
            obsolete.write_bytes(b"unknown local work")
            with self.assertRaisesRegex(ValueError, "changed obsolete Fortress asset"):
                module.import_assets(source, output)
            self.assertEqual(obsolete.read_bytes(), b"unknown local work")

    def test_synthetic_import_rejects_output_symlink_escape_before_mutation(self):
        module = importer()
        with tempfile.TemporaryDirectory() as temporary:
            source, output, outside = (Path(temporary) / name for name in ("inputs", "output", "outside"))
            self.synthetic_packages(module, source)
            output.mkdir()
            outside.mkdir()
            (output / "sprites").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "escapes external output root"):
                module.import_assets(source, output)
            self.assertEqual(list(outside.iterdir()), [])

    def test_shipped_fortress_binary_resources_are_present_and_verified(self):
        for level in range(1, 6):
            self.assertGreater(len((SPRITES / ("TBFRMAGE.def" if level == 1 else f"TBFRMAG{level}.def")).read_bytes()), 100)
            for name in (f"BoFMage{level}.pcx", f"TOFMAG{level}A.bmp", f"TZFMAG{level}A.bmp"):
                self.assertGreater(len((DATA / name).read_bytes()), 100)

    def test_v9_archive_root_and_directory_case_are_normalized(self):
        module = importer()
        payload = BytesIO()
        with ZipFile(payload, "w") as archive:
            archive.writestr("fortress-mage-guild/content/Data/BoFMage1.pcx", b"synthetic pcx")
            archive.writestr("fortress-mage-guild/content/Sprites/TBFRMAGE.def", b"synthetic def")
        with ZipFile(payload) as archive:
            self.assertEqual(module._content_root(archive), ("fortress-mage-guild", "content"))
            self.assertTrue(any(name.startswith("fortress-mage-guild/content/Data/") for name in archive.namelist()))
            self.assertTrue(any(name.startswith("fortress-mage-guild/content/Sprites/") for name in archive.namelist()))

        for name in ("TBFRMAGE.def", "TBFRMAG2.def", "TBFRMAG3.def", "TBFRMAG4.def", "TBFRMAG5.def"):
            self.assertTrue((SPRITES / name).is_file(), name)
        for name in ("BoFMage1.pcx", "TOFMAG1A.bmp", "TZFMAG1A.bmp"):
            self.assertTrue((DATA / name).is_file(), name)

    def test_shipped_fortress_hall_cards_are_native_size(self):
        for level in range(1, 6):
            image = open_image(f"SPRITES/fortress-mage-guild-hall-{level}.png")
            self.assertEqual(image.size, (150, 70))
            self.assertEqual(image.mode, "RGB")

    def test_synthetic_import_preserves_binary_and_normalizes_descriptor_paths(self):
        module = importer()
        payload = BytesIO()
        with ZipFile(payload, "w") as archive:
            archive.writestr("guild/Content/Sprites/test.json", json.dumps({"basepath": "nested\\frames", "images": []}))
            archive.writestr("guild/Content/Data/test.bin", b"unchanged synthetic binary")
        with ZipFile(payload) as archive:
            assets = dict(module._runtime_assets(archive))
        self.assertEqual(assets[Path("data/test.bin")], b"unchanged synthetic binary")
        self.assertEqual(json.loads(assets[Path("sprites/test.json")])["basepath"], "nested/frames/")

    def test_all_animation_frames_resolve_with_preserved_counts(self):
        sprites = SPRITES
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
            self.assertTrue((SPRITES / animation).is_file())
            self.assertTrue((DATA / campaign).is_file())
            self.assertTrue((DATA / border).is_file())
            self.assertTrue((DATA / area).is_file())

        obsolete = [CONTENT / "sprites" / f"TBFRMAG{level}.json" for level in (4, 5)]
        obsolete.extend(CONTENT / "sprites" / f"TBFRMAG{level}" for level in (4, 5))
        obsolete.extend(CONTENT / "sprites" / "HALLFORT" / f"mage-guild-{level}.png" for level in (4, 5))
        for path in obsolete:
            self.assertFalse(path.exists(), str(path))

    def test_all_authored_mage_guild_names_use_arabic_level_numbers(self):
        overlay = load(CONTENT / "config/factions/universalMageGuilds.json")
        expected_factions = {
            "core:castle", "core:rampart", "core:tower", "core:inferno", "core:necropolis",
            "core:dungeon", "core:stronghold", "core:fortress", "core:conflux",
        }
        self.assertEqual(set(overlay), expected_factions)

        expected_authored_names = {
            "core:castle": {"mageGuild5": "Mage Guild Level 5"},
            "core:stronghold": {
                "mageGuild4": "Mage Guild Level 4",
                "mageGuild5": "Mage Guild Level 5",
            },
            "core:fortress": {
                "mageGuild4": "Mage Guild Level 4",
                "mageGuild5": "Mage Guild Level 5",
            },
        }
        actual_authored_names = {}
        for faction, row in overlay.items():
            town = row["town"]
            if "mageGuild" in town:
                self.assertEqual(town["mageGuild"], 5, faction)
            buildings = town.get("buildings", {})
            faction_names = {}
            for level in range(1, 6):
                building = buildings.get(f"mageGuild{level}", {})
                if "name" not in building:
                    continue
                self.assertEqual(building["name"], f"Mage Guild Level {level}", (faction, level))
                faction_names[f"mageGuild{level}"] = building["name"]
            if faction_names:
                actual_authored_names[faction] = faction_names

        # Only these existing faction overrides author guild names; all other
        # factions retain their inherited normal tier translations.
        self.assertEqual(actual_authored_names, expected_authored_names)

    def test_hall_overrides_only_replace_intended_frames(self):
        for _, _, hall, expected in PACKAGES.values():
            descriptor = load(SPRITES / (hall + ".json"))
            self.assertNotIn("sequences", descriptor)
            self.assertEqual({item["frame"] for item in descriptor["images"]}, expected)
            for item in descriptor["images"]:
                self.assertEqual(item["group"], 0)
                if "file" in item:
                    self.assertTrue((SPRITES / (descriptor.get("basepath", "") + item["file"])).is_file())
                else:
                    # Selected Castle card is an explicit original-resource
                    # alias; unchanged purchaser assets are intentionally external.
                    self.assertEqual(hall, "HALLCSTL")
                    self.assertEqual(item, {"group": 0, "frame": 4, "defFile": "HALLCSTL.def", "defGroup": 0, "defFrame": 3})

    def test_shipped_structure_registration_preserves_nh_rules(self):
        overlay = load(CONTENT / "config/factions/universalMageGuilds.json")
        for faction, (package, config, hall, _) in PACKAGES.items():
            actual = overlay["core:" + faction]["town"]
            self.assertTrue(actual["structures"])
            for structure in actual["structures"].values():
                self.assertIn("animation", structure)
                self.assertIsInstance(structure["x"], int)
                self.assertIsInstance(structure["y"], int)
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
