#!/usr/bin/env python3
"""Offline contract checks for New Horizons main-menu title replacements."""

import json
import re
import struct
import unittest
from collections import Counter
from pathlib import Path
if __package__:
    from .nhart_test_resources import ArtPath
else:
    from nhart_test_resources import ArtPath


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_REPLACEMENTS = {
    ("GAMSELBK", 768338213): ("NH_menu_complete_main", 8, 120, 432, 65),
    ("GAMSELB0", 3193207774): ("NH_menu_complete_scenario0", 10, 92, 385, 57),
    ("GAMSELB1", 3653370809): ("NH_menu_complete_scenario1", 10, 92, 385, 57),
    ("LOADBAR", 78987791): ("NH_menu_complete_loading", 140, 116, 466, 70),
    ("GAMSELBK", 3882180678): ("NH_menu_ab_main", 12, 106, 458, 46),
    ("GAMSELB0", 501462061): ("NH_menu_ab_scenario0", 18, 82, 370, 40),
    ("GAMSELB1", 3897961312): ("NH_menu_ab_scenario1", 18, 82, 370, 40),
    ("LOADBAR", 1566987993): ("NH_menu_ab_loading", 10, 112, 460, 47),
}
EXPECTED_BACKGROUNDS = {
    "scenario-selection": ["NH_MENU_GAMSELB0", "NH_MENU_GAMSELB1"],
    "loading": ["NH_MENU_LOADBAR"],
    "window": ["NH_MENU_GAMSELBK"],
}
ORIGINAL_RESOURCES = {"GAMSELBK", "GAMSELB0", "GAMSELB1", "LOADBAR"}


def png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise AssertionError(f"{path} is not a PNG with an IHDR chunk")
    return struct.unpack(">II", data[16:24])


def background_array(config_text: str, section: str) -> list[str]:
    section_match = re.search(
        rf'"{re.escape(section)}"\s*:\s*\{{.*?"background"\s*:\s*\[([^\]]*)\]',
        config_text,
        re.DOTALL,
    )
    if not section_match:
        raise AssertionError(f"mainmenu.json has no background array for {section}")
    return re.findall(r'"([^"\\]+)"', section_match.group(1))


class NewHorizonsMenuTitleTests(unittest.TestCase):
    def test_metadata_has_exact_resource_crc_patch_and_position_mappings(self):
        metadata = json.loads((ROOT / "config/newHorizonsMenuArt.json").read_text(encoding="utf-8"))
        rows = metadata["replacements"]
        actual = {
            (row["resource"], row["crc32"]): (
                row["image"], row["x"], row["y"], row["width"], row["height"]
            )
            for row in rows
        }

        self.assertEqual(len(rows), len(EXPECTED_REPLACEMENTS))
        self.assertEqual(actual, EXPECTED_REPLACEMENTS)
        self.assertEqual(
            Counter(row["resource"] for row in rows),
            Counter({resource: 2 for resource in ORIGINAL_RESOURCES}),
        )

        for row in rows:
            with self.subTest(resource=row["resource"], crc32=row["crc32"]):
                patch_path = ArtPath() / f"{row['image']}.png"
                self.assertTrue(patch_path.is_file(), f"missing generated patch {patch_path.name}")
                self.assertEqual(png_dimensions(patch_path), (row["width"], row["height"]))
                self.assertGreaterEqual(row["x"], 0)
                self.assertGreaterEqual(row["y"], 0)
                self.assertLessEqual(row["x"] + row["width"], 800)
                self.assertLessEqual(row["y"] + row["height"], 600)

    def test_main_menu_uses_only_the_four_generated_wrapper_names(self):
        config_text = (ROOT / "config/mainmenu.json").read_text(encoding="utf-8")
        used_backgrounds = []
        for section, expected in EXPECTED_BACKGROUNDS.items():
            with self.subTest(section=section):
                actual = background_array(config_text, section)
                self.assertEqual(actual, expected)
                used_backgrounds.extend(actual)

        for resource in ORIGINAL_RESOURCES:
            self.assertNotIn(resource.lower(), {name.lower() for name in used_backgrounds})

    def test_four_lazy_wrappers_match_the_original_resources(self):
        source = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
        header = (ROOT / "client/render/AssetGenerator.h").read_text(encoding="utf-8")
        bindings = {
            generated: resource
            for generated, resource in re.findall(
                r'imageFiles\[ImagePath::builtin\("(NH_MENU_[A-Z0-9]+)"\)\]\s*=\s*\[this\]\(\)\{ return createNewHorizonsMenuTitleImage\("([A-Z0-9]+)"\); \};',
                source,
            )
        }
        self.assertEqual(
            bindings,
            {
                "NH_MENU_GAMSELBK": "GAMSELBK",
                "NH_MENU_GAMSELB0": "GAMSELB0",
                "NH_MENU_GAMSELB1": "GAMSELB1",
                "NH_MENU_LOADBAR": "LOADBAR",
            },
        )
        source_tokens = (
            "ImagePath::builtin(resource), EImageBlitMode::OPAQUE",
            'const std::array<std::string, 3> pathPrefixes = { "SPRITES/", "DATA/", "" }',
            "ResourcePath candidate(prefix + resource, EResType::IMAGE)",
            "const std::string cacheKey = sourcePath.getName()",
            "sourceStream->calculateCRC32()",
            "menuTitleSourceCrc32.find(cacheKey)",
            'ImagePath::builtin("SPRITES/" + match->image)',
            "EImageBlitMode::SIMPLE",
            "patch->width() > original->width() - match->x",
            "patch->height() > original->height() - match->y",
            "Unknown New Horizons menu title source variant",
            "New Horizons menu title patch %s for %s is missing",
        )
        for token in source_tokens:
            with self.subTest(token=token):
                self.assertTrue(token in source, f"missing wrapper contract: {token}")
        self.assertTrue("createNewHorizonsMenuTitleImage" in header)

    def test_original_resource_names_are_not_live_menu_backgrounds(self):
        config_text = (ROOT / "config/mainmenu.json").read_text(encoding="utf-8")
        live_backgrounds = {
            name.upper()
            for section in EXPECTED_BACKGROUNDS
            for name in background_array(config_text, section)
        }
        self.assertTrue(live_backgrounds.isdisjoint(ORIGINAL_RESOURCES), live_backgrounds)

    def test_missing_source_does_not_dereference_a_null_generated_canvas(self):
        source = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
        self.assertIn("return generated ? generated->toSharedImage() : nullptr", source)
        self.assertIn("if(generated)\n\t\t\tresult[entry.first] = generated->toSharedImage();", source)
        self.assertNotIn("imageFiles.at(image)()->toSharedImage()", source)
        self.assertNotIn("entry.second()->toSharedImage()", source)


if __name__ == "__main__":
    unittest.main()
