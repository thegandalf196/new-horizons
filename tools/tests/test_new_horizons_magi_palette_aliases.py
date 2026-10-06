#!/usr/bin/env python3
"""Focused contract tests for the JSON-only Magi palette alias generator."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import create_new_horizons_magi_palette_aliases as aliases  # noqa: E402


VALID_MAPS = {
    "archBattle": {"40": [180, 180, 180]},
    "mageProjectile": {"244": [220, 30, 24]},
    "archPortrait": {"40": [180, 180, 180]},
}
OPTIONAL_MAPS = {
    **VALID_MAPS,
    "archSmall": {"40": [180, 180, 180]},
    "archEncounter": {"40": [180, 180, 180]},
    "archMap": {"40": [180, 180, 180]},
}


class NewHorizonsMagiPaletteAliasesTest(unittest.TestCase):
    def test_aliases_preserve_exact_external_group_frame_routes(self):
        descriptors = aliases.build_descriptors(VALID_MAPS)
        self.assertEqual(
            set(descriptors),
            {
                "NH_ArchMageGrey.json",
                "NH_MageRedProjectile.json",
                "NH_ArchMageGreyPortrait.json",
            },
        )

        arch_battle = descriptors["NH_ArchMageGrey.json"]
        self.assertEqual(arch_battle["paletteRemap"], VALID_MAPS["archBattle"])
        expected_arch_frames = [
            (group, frame)
            for group, count in aliases.ARCH_MAGE_GROUP_FRAME_COUNTS.items()
            for frame in range(count)
        ]
        self.assertEqual(len(expected_arch_frames), 133)
        self.assertEqual(
            [(entry["group"], entry["frame"]) for entry in arch_battle["images"]],
            expected_arch_frames,
        )
        self.assertEqual(
            [(entry["defGroup"], entry["defFrame"]) for entry in arch_battle["images"]],
            expected_arch_frames,
        )
        self.assertTrue(all(entry["defFile"] == "CAMAGE.DEF" for entry in arch_battle["images"]))

        projectile = descriptors["NH_MageRedProjectile.json"]
        self.assertEqual(projectile["paletteRemap"], VALID_MAPS["mageProjectile"])
        self.assertEqual(len(projectile["images"]), 9)
        self.assertEqual(
            [(entry["group"], entry["frame"], entry["defGroup"], entry["defFrame"])
             for entry in projectile["images"]],
            [(0, frame, 0, frame) for frame in range(9)],
        )
        self.assertTrue(all(entry["defFile"] == "PMAGEX.DEF" for entry in projectile["images"]))

        portrait = descriptors["NH_ArchMageGreyPortrait.json"]
        self.assertEqual(portrait["paletteRemap"], VALID_MAPS["archPortrait"])
        self.assertEqual(portrait["images"], [{
            "group": 0,
            "frame": 0,
            "defFile": "TWCRPORT.DEF",
            "defGroup": 0,
            "defFrame": 37,
        }])

        # The output contains only mappings and explicit frame references: no
        # extracted or modified source image payload is ever generated.
        for descriptor in descriptors.values():
            self.assertEqual(set(descriptor), {"paletteRemap", "images"})
            self.assertTrue(all(set(frame) == {
                "group", "frame", "defFile", "defGroup", "defFrame"
            } for frame in descriptor["images"]))

    def test_optional_archmage_aliases_use_only_explicit_source_frames(self):
        descriptors = aliases.build_descriptors(OPTIONAL_MAPS)
        self.assertEqual(
            set(descriptors),
            {
                "NH_ArchMageGrey.json",
                "NH_MageRedProjectile.json",
                "NH_ArchMageGreyPortrait.json",
                "NH_ArchMageGreySmall.json",
                "NH_ArchMageGreyEncounter.json",
                "NH_ArchMageGreyMap.json",
            },
        )
        self.assertEqual(descriptors["NH_ArchMageGreySmall.json"], {
            "paletteRemap": OPTIONAL_MAPS["archSmall"],
            "images": [{
                "group": 0,
                "frame": 0,
                "defFile": "CPRSMALL.DEF",
                "defGroup": 0,
                "defFrame": 37,
            }],
        })
        self.assertEqual(descriptors["NH_ArchMageGreyEncounter.json"], {
            "paletteRemap": OPTIONAL_MAPS["archEncounter"],
            "images": [
                {
                    "group": 0,
                    "frame": 0,
                    "defFile": "AvWattak.DEF",
                    "defGroup": 0,
                    "defFrame": 70,
                },
                {
                    "group": 0,
                    "frame": 1,
                    "defFile": "AvWattak.DEF",
                    "defGroup": 0,
                    "defFrame": 71,
                },
            ],
        })
        map_descriptor = descriptors["NH_ArchMageGreyMap.json"]
        self.assertEqual(map_descriptor["paletteRemap"], OPTIONAL_MAPS["archMap"])
        self.assertEqual(aliases.ARCH_MAGE_MAP_CANVAS_SIZE, (64, 64))
        self.assertEqual(aliases.ARCH_MAGE_MAP_GROUP_FRAME_COUNTS, {0: 30})
        self.assertEqual(len(map_descriptor["images"]), 30)
        self.assertEqual(
            [(entry["group"], entry["frame"], entry["defFile"], entry["defGroup"], entry["defFrame"])
             for entry in map_descriptor["images"]],
            [(0, frame, "AVWmagx0.DEF", 0, frame) for frame in range(30)],
        )

        # Each role is opt-in independently; omitted maps do not create files.
        small_only = aliases.build_descriptors({**VALID_MAPS, "archSmall": OPTIONAL_MAPS["archSmall"]})
        self.assertIn("NH_ArchMageGreySmall.json", small_only)
        self.assertNotIn("NH_ArchMageGreyEncounter.json", small_only)
        encounter_only = aliases.build_descriptors({**VALID_MAPS, "archEncounter": OPTIONAL_MAPS["archEncounter"]})
        self.assertIn("NH_ArchMageGreyEncounter.json", encounter_only)
        self.assertNotIn("NH_ArchMageGreySmall.json", encounter_only)
        map_only = aliases.build_descriptors({**VALID_MAPS, "archMap": OPTIONAL_MAPS["archMap"]})
        self.assertIn("NH_ArchMageGreyMap.json", map_only)
        self.assertNotIn("NH_ArchMageGreySmall.json", map_only)
        self.assertNotIn("NH_ArchMageGreyEncounter.json", map_only)

    def test_optional_authored_maps_load_and_unknown_names_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            maps_path = Path(temp_dir) / "maps.json"
            maps_path.write_text(json.dumps(OPTIONAL_MAPS), encoding="utf-8")
            self.assertEqual(aliases.load_authored_maps(maps_path), OPTIONAL_MAPS)

            maps_path.write_text(json.dumps({**VALID_MAPS, "unreviewedRole": {"40": [1, 2, 3]}}),
                                 encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown maps: unreviewedRole"):
                aliases.load_authored_maps(maps_path)
        with self.assertRaisesRegex(ValueError, "unknown maps: unreviewedRole"):
            aliases.build_descriptors({**VALID_MAPS, "unreviewedRole": {"40": [1, 2, 3]}})

    def test_maps_are_normalized_and_descriptor_json_is_stable(self):
        reordered = {
            "archBattle": {"80": [8, 8, 8], "40": [4, 4, 4]},
            "mageProjectile": VALID_MAPS["mageProjectile"],
            "archPortrait": VALID_MAPS["archPortrait"],
        }
        reverse = {
            "archBattle": {"40": [4, 4, 4], "80": [8, 8, 8]},
            "mageProjectile": VALID_MAPS["mageProjectile"],
            "archPortrait": VALID_MAPS["archPortrait"],
        }
        first = aliases.build_descriptors(reordered)
        second = aliases.build_descriptors(reverse)
        self.assertEqual(
            {name: aliases.descriptor_bytes(value) for name, value in first.items()},
            {name: aliases.descriptor_bytes(value) for name, value in second.items()},
        )
        self.assertEqual(first["NH_ArchMageGrey.json"]["paletteRemap"], {
            "40": [4, 4, 4], "80": [8, 8, 8]
        })

    def test_invalid_or_ambiguous_maps_are_rejected(self):
        invalid_maps = [
            {"7": [1, 2, 3]},
            {"08": [1, 2, 3]},
            {"256": [1, 2, 3]},
            {"40": [1, 2]},
            {"40": [1, 2, 256]},
            {"40": [1, True, 3]},
            {"40": [1.0, 2, 3]},
            {},
        ]
        for invalid in invalid_maps:
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    aliases.validate_palette_map(invalid, "test")

        with tempfile.TemporaryDirectory() as temp_dir:
            maps_path = Path(temp_dir) / "duplicate.json"
            maps_path.write_text(
                '{"archBattle":{"40":[1,2,3],"40":[4,5,6]},'
                '"mageProjectile":{"40":[1,2,3]},"archPortrait":{"40":[1,2,3]}}',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "duplicate JSON object key"):
                aliases.load_authored_maps(maps_path)

    def test_write_check_and_unknown_existing_bytes_are_safe(self):
        descriptors = aliases.build_descriptors(VALID_MAPS)
        with tempfile.TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir) / "aliases"
            aliases.write_or_check(output_dir, descriptors, check=False)
            aliases.write_or_check(output_dir, descriptors, check=True)

            target = output_dir / "NH_ArchMageGrey.json"
            target.write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
                aliases.write_or_check(output_dir, descriptors, check=False)

        runtime_dir = ROOT / "Mods" / "new-horizons" / "Content" / "sprites"
        self.assertEqual(aliases._safe_output_directory(runtime_dir, allow_runtime_read=True), runtime_dir.resolve())
        with self.assertRaisesRegex(ValueError, "must not be inside Mods"):
            aliases._safe_output_directory(runtime_dir)
        with self.assertRaisesRegex(ValueError, "must not be inside Mods"):
            aliases.write_or_check(runtime_dir, descriptors, check=False)

    def test_committed_aliases_match_maps_and_runtime_check_is_read_only(self):
        maps_path = ROOT / "assets/new-horizons/creatures/magi-palette/v1/maps.json"
        runtime_dir = ROOT / "Mods/new-horizons/Content/sprites"
        descriptors = aliases.build_descriptors(aliases.load_authored_maps(maps_path))

        with tempfile.TemporaryDirectory() as temp_dir:
            generated_dir = Path(temp_dir) / "generated"
            aliases.write_or_check(generated_dir, descriptors, check=False)
            aliases.write_or_check(generated_dir, descriptors, check=True)
            aliases.write_or_check(runtime_dir, descriptors, check=True)

            generated = {
                name: json.loads((generated_dir / name).read_text(encoding="utf-8"))
                for name in descriptors
            }
            installed = {
                name: json.loads((runtime_dir / name).read_text(encoding="utf-8"))
                for name in descriptors
            }
            self.assertEqual(generated, descriptors)
            self.assertEqual(installed, descriptors)

        before = {name: (runtime_dir / name).read_bytes() for name in descriptors}
        with redirect_stdout(io.StringIO()):
            self.assertEqual(aliases.main([
                "--maps", str(maps_path), "--output-dir", str(runtime_dir), "--check"
            ]), 0)
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as rejected_write:
                aliases.main(["--maps", str(maps_path), "--output-dir", str(runtime_dir)])
        self.assertEqual(rejected_write.exception.code, 2)
        self.assertEqual(before, {name: (runtime_dir / name).read_bytes() for name in descriptors})


if __name__ == "__main__":
    unittest.main()
