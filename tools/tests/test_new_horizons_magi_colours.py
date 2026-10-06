#!/usr/bin/env python3
"""Check the Arch Mage ray colour patch preserves its authored beam geometry."""

import copy
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
STRING = r'"(?:\\.|[^"\\])*"'
EXPECTED_RED_RGB = [
    [192, 32, 24],
    [232, 48, 40],
    [255, 64, 48],
    [232, 48, 40],
    [192, 32, 24],
]


def parse_jsonc(text):
    """Parse repository JSON-with-comments and trailing commas."""
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


def load(relative):
    return parse_jsonc((ROOT / relative).read_text(encoding="utf-8"))


def merge_objects(base, patch):
    """Apply VCMI's recursive-object and replace-array patch semantics."""
    for patch_key, value in patch.items():
        override = patch_key.endswith("#override")
        key = patch_key.removesuffix("#override") if override else patch_key
        if override or not (isinstance(value, dict) and isinstance(base.get(key), dict)):
            base[key] = copy.deepcopy(value)
        else:
            merge_objects(base[key], value)


class NewHorizonsMagiColoursTest(unittest.TestCase):
    def test_arch_mage_rays_turn_red_without_changing_beam_timing_or_alpha(self):
        core = load("config/creatures/tower.json")
        module = load("Mods/new-horizons/Content/config/creatures/tower.json")

        core_arch_mage = copy.deepcopy(core["archMage"])
        arch_mage_patch = module["core:archMage"]
        missile_patch = arch_mage_patch["graphics"]["missile"]
        self.assertEqual(set(missile_patch), {"ray"})

        base_missile = core_arch_mage["graphics"]["missile"]
        self.assertEqual(base_missile["attackClimaxFrame"], 8)
        expected_alpha = [
            ([ray["start"][3], ray["end"][3]]) for ray in base_missile["ray"]
        ]

        merge_objects(core_arch_mage, arch_mage_patch)
        merged_missile = core_arch_mage["graphics"]["missile"]
        self.assertEqual(merged_missile["attackClimaxFrame"], 8)
        self.assertEqual(len(merged_missile["ray"]), 5)
        self.assertEqual(len(base_missile["ray"]), 5)

        # Every non-ray missile field (including timing, offsets and angles) remains
        # the inherited core value; only RGB changes, never the authored alpha.
        self.assertEqual(
            {key: value for key, value in merged_missile.items() if key != "ray"},
            {key: value for key, value in base_missile.items() if key != "ray"},
        )
        for index, (ray, base_ray, expected_rgb) in enumerate(
            zip(merged_missile["ray"], base_missile["ray"], EXPECTED_RED_RGB)
        ):
            with self.subTest(ray=index):
                self.assertEqual(set(ray), {"start", "end"})
                self.assertEqual(ray["start"][:3], expected_rgb)
                self.assertEqual(ray["end"][:3], expected_rgb)
                self.assertGreater(expected_rgb[0], expected_rgb[1])
                self.assertGreater(expected_rgb[0], expected_rgb[2])
                self.assertEqual(
                    [ray["start"][3], ray["end"][3]], expected_alpha[index]
                )
                self.assertEqual(len(base_ray["start"]), 4)
                self.assertEqual(len(base_ray["end"]), 4)

        # Mage changes only the projectile resource; inherited release timing,
        # offsets and angles remain intact. Its robe animation stays original.
        mage = copy.deepcopy(core["mage"])
        merge_objects(mage, module["core:mage"])
        self.assertEqual(mage["graphics"]["animation"], core["mage"]["graphics"]["animation"])
        self.assertEqual(mage["graphics"]["missile"]["projectile"], "NH_MageRedProjectile.def")
        self.assertEqual(
            {key: value for key, value in mage["graphics"]["missile"].items() if key != "projectile"},
            {key: value for key, value in core["mage"]["graphics"]["missile"].items() if key != "projectile"},
        )
        self.assertEqual(
            arch_mage_patch["graphics"]["iconLarge"],
            "NH_academy_archMage_icon_large.png",
        )


if __name__ == "__main__":
    unittest.main()
