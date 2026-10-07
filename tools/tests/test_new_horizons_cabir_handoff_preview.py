"""Focused checks for the isolated full-state Cabir handoff preview overlay."""

import json
from pathlib import Path
import re
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_new_horizons_cabir_handoff_preview as preview  # noqa: E402


class CabirHandoffPreviewTest(unittest.TestCase):
    def _partial_files(self):
        files = {}
        forms = {
            "cabir": ("NH_CabirHandoff", "cabir-handoff/cabir"),
            "cabir-master": ("NH_CabirMasterHandoff", "cabir-handoff/cabir-master"),
        }
        for key, (descriptor_name, base) in forms.items():
            sequences = [
                {"group": 0, "generateOverlay": 1, "frames": [f"walk/frame-{i:02}.png" for i in range(7)]},
                {"group": 2, "generateOverlay": 1, "frames": ["holding/frame-00.png"]},
                {"group": 12, "generateOverlay": 1, "frames": [f"melee/frame-{i:02}.png" for i in range(6)]},
                {"group": 15, "generateOverlay": 1, "frames": [f"ranged/frame-{i:02}.png" for i in range(6)]},
            ]
            descriptor = {"basepath": f"{base}/battle/", "sequences": sequences}
            descriptor_path = f"Mods/new-horizons/Content/sprites/{descriptor_name}.json"
            files[descriptor_path] = preview._json_bytes(descriptor)
            for sequence in sequences:
                for frame in sequence["frames"]:
                    files[f"Mods/new-horizons/Images/{descriptor['basepath']}{frame}"] = (
                        f"{key}:{sequence['group']}:{frame}".encode()
                    )
            map_name = f"{descriptor_name}Map"
            map_desc = {
                "basepath": f"{base}/map/",
                "sequences": [{"group": 0, "frames": [f"walk/frame-{i:02}.png" for i in range(7)]}],
            }
            files[f"Mods/new-horizons/Content/sprites/{map_name}.json"] = preview._json_bytes(map_desc)
            for frame in map_desc["sequences"][0]["frames"]:
                files[f"Mods/new-horizons/Images/{map_desc['basepath']}{frame}"] = f"map:{key}:{frame}".encode()

        projectile = {
            "basepath": "cabir-handoff/projectile/",
            "images": [{"group": 0, "frame": i, "file": f"frame-{i:02}.png"} for i in range(9)],
        }
        files["Mods/new-horizons/Content/sprites/NH_CabirHandoffFireball.json"] = preview._json_bytes(projectile)
        for frame in projectile["images"]:
            files[f"Mods/new-horizons/Images/{projectile['basepath']}{frame['file']}"] = f"fire:{frame['frame']}".encode()
        # Supplied icon paths are checked as config references but their bytes
        # are opaque here; source-file fidelity is tested by equality below.
        for binding in preview.FORM_BINDINGS.values():
            for key in ("iconLarge", "iconSmall"):
                files[f"Mods/new-horizons/Images/{binding[key]}"] = b"pinned supplied icon bytes"
        return files

    @staticmethod
    def _tower_config():
        entry = {
            "name": {"singular": "Name", "plural": "Names"},
            "abilities": {"shooter": {"type": "SHOOTER"}},
            "shots": 8,
            "graphics": {
                "animation": "legacy.def",
                "map": "legacy-map.def",
                "mapMask": ["VV", "VA"],
                "mapAttackFromLeft": "legacy-left.png",
                "mapAttackFromRight": "legacy-right.png",
                "iconLarge": "legacy-large.png",
                "iconSmall": "legacy-small.png",
                "animationTime": {"attack": 1.0, "walk": 0.75, "idle": 1.0},
                "missile": {
                    "projectile": "legacy-projectile.DEF",
                    "frameAngles": preview.FRAME_ANGLES,
                    "attackClimaxFrame": 3,
                    "offset": {"upperX": 1, "upperY": -1, "middleX": 2, "middleY": -2, "lowerX": 3, "lowerY": -3},
                },
            },
        }
        return {
            "core:gremlin": json.loads(json.dumps(entry)),
            "core:masterGremlin": json.loads(json.dumps(entry)),
            "core:unrelated": {"name": {"singular": "Other"}, "graphics": {"iconLarge": "other.png"}},
        }

    def test_all_32_groups_load_and_only_four_groups_use_supplied_sequences(self):
        source_files = self._partial_files()
        original_files = dict(source_files)
        tower_before = preview._json_bytes(self._tower_config())
        built, report = preview.build_preview_overlay(source_files, tower_before)

        expected_groups = [group for group, _name in preview.CREATURE_GROUPS]
        self.assertEqual(len(expected_groups), 32)
        constants = (ROOT / "client/battle/BattleConstants.h").read_text(encoding="utf-8")
        enum_body = re.search(r"enum class ECreatureAnimType\s*\{(.*?)\};", constants, re.S).group(1)
        declared = [
            int(value)
            for _name, value in re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)", enum_body, re.M)
            if value != "-1"
        ]
        self.assertEqual(expected_groups, declared)

        for binding in preview.FORM_BINDINGS.values():
            relative = f"Mods/new-horizons/Content/sprites/{binding['battleDescriptor'].replace('.def', '.json')}"
            descriptor = json.loads(built[relative])
            self.assertEqual([sequence["group"] for sequence in descriptor["sequences"]], expected_groups)
            by_group = {sequence["group"]: sequence for sequence in descriptor["sequences"]}
            original = json.loads(original_files[relative])
            original_groups = {sequence["group"]: sequence for sequence in original["sequences"]}
            for group in (0, 2, 12, 15):
                self.assertEqual(by_group[group], original_groups[group])
            self.assertEqual(by_group[5]["frames"], by_group[2]["frames"])
            self.assertEqual(by_group[6]["frames"], by_group[2]["frames"])
            self.assertEqual(by_group[11]["frames"], by_group[12]["frames"])
            self.assertEqual(by_group[14]["frames"], by_group[15]["frames"])
            self.assertEqual(by_group[20]["frames"], [by_group[0]["frames"][0]])
            self.assertEqual(by_group[21]["frames"], [by_group[0]["frames"][-1]])
            for sequence in descriptor["sequences"]:
                for frame in sequence["frames"]:
                    image_path = f"Mods/new-horizons/Images/{descriptor['basepath']}{frame}"
                    self.assertIn(image_path, built)

        patched_battle_descriptors = {
            f"Mods/new-horizons/Content/sprites/{binding['battleDescriptor'].replace('.def', '.json')}"
            for binding in preview.FORM_BINDINGS.values()
        }
        self.assertTrue(
            all(built[path] == data for path, data in original_files.items() if path not in patched_battle_descriptors),
            "source frames, map/projectile descriptors and icons must remain byte-identical",
        )
        self.assertEqual(set(report["creatures"]["cabir"]["aliasGroups"]), {str(g) for g in preview.GROUP_ALIASES})
        self.assertIn("corpse pose supplied", report["creatures"]["cabir"]["aliasGroups"]["5"]["reason"])
        self.assertFalse(report["normalPlayableChanged"])
        self.assertFalse(report["gameplayChanged"])

    def test_tower_overlay_changes_only_cabir_graphics_bindings(self):
        before = self._tower_config()
        files, _report = preview.build_preview_overlay(self._partial_files(), preview._json_bytes(before))
        patched = json.loads(files[f"Mods/new-horizons/{preview.CREATURES_RELATIVE}"])
        self.assertEqual(patched["core:unrelated"], before["core:unrelated"])

        for form, binding in preview.FORM_BINDINGS.items():
            creature_id = binding["creatureId"]
            old = before[creature_id]
            new = patched[creature_id]
            self.assertEqual(new["name"], old["name"])
            self.assertEqual(new["abilities"], old["abilities"])
            self.assertEqual(new["shots"], old["shots"])
            graphics = new["graphics"]
            self.assertEqual(graphics["animation"], binding["battleDescriptor"])
            self.assertEqual(graphics["map"], binding["mapDescriptor"])
            self.assertEqual(graphics["mapAttackFromLeft"], binding["mapAttack"])
            self.assertEqual(graphics["mapAttackFromRight"], binding["mapAttack"])
            self.assertEqual(graphics["iconLarge"], binding["iconLarge"])
            self.assertEqual(graphics["iconSmall"], binding["iconSmall"])
            self.assertEqual(graphics["animationTime"], old["graphics"]["animationTime"])
            self.assertEqual(graphics["mapMask"], old["graphics"]["mapMask"])
            missile = graphics["missile"]
            self.assertEqual(missile["projectile"], binding["projectile"])
            self.assertEqual(missile["attackClimaxFrame"], 4)
            self.assertEqual(missile["frameAngles"], preview.FRAME_ANGLES)
            self.assertEqual(missile["offset"], {
                "upperX": 33, "upperY": -42,
                "middleX": 33, "middleY": -42,
                "lowerX": 33, "lowerY": -42,
            })

        self.assertIn("portrait-cutout artifacts are retained but unused", preview.build_preview_overlay(
            self._partial_files(), preview._json_bytes(before)
        )[1]["portraitBindings"])

    def test_descriptor_and_config_changes_are_reproducible_and_source_stays_immutable(self):
        source_files = self._partial_files()
        baseline = preview._json_bytes(self._tower_config())
        first, first_report = preview.build_preview_overlay(source_files, baseline)
        second, second_report = preview.build_preview_overlay(source_files, baseline)
        self.assertEqual(first, second)
        self.assertEqual(first_report["outputFileHashes"], second_report["outputFileHashes"])
        self.assertEqual(source_files, self._partial_files())
        self.assertEqual(len(first_report["outputFileHashes"]), len(first))

    def test_unknown_descriptor_groups_and_invalid_output_paths_fail_closed(self):
        files = self._partial_files()
        path = "Mods/new-horizons/Content/sprites/NH_CabirHandoff.json"
        descriptor = json.loads(files[path])
        descriptor["sequences"].append({"group": 9, "frames": ["walk/frame-00.png"]})
        files[path] = preview._json_bytes(descriptor)
        with self.assertRaisesRegex(ValueError, "groups changed unexpectedly"):
            preview.build_preview_overlay(files, preview._json_bytes(self._tower_config()))

        with self.assertRaisesRegex(ValueError, "below repository build"):
            preview._validate_output_path(ROOT / "output/cabir-preview")


if __name__ == "__main__":
    unittest.main()
