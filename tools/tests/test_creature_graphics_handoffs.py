import importlib.util
import copy
import json
from pathlib import Path
import re
import unittest

path = Path(__file__).resolve().parents[1] / "ci/apply_creature_graphics_handoffs.py"
spec = importlib.util.spec_from_file_location("graphics_handoffs", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GraphicsMergeTests(unittest.TestCase):
    def test_handoffs_after_source_refresh_preserve_all_gameplay_and_sounds(self):
        # Delivery order: refresh the creature configuration from source first,
        # then restore graphics-only handoffs, never an older whole creature file.
        source_path = path.parents[2] / "Mods/new-horizons/Content/config/creatures/tower.json"
        refreshed = json.loads(re.sub(r"(?m)^\s*//[^\n]*(?:\n|$)", "\n", source_path.read_text()))
        creature_ids = ("core:gremlin", "core:masterGremlin", "core:mage", "core:archMage")
        for index, creature_id in enumerate(creature_ids):
            # Deliberately new gameplay content unknown to the graphics handoff.
            refreshed[creature_id]["sourceRefreshMarker"] = {"revision": index, "nested": [1, None, {"new": True}]}
            refreshed[creature_id].setdefault("sound", {})["sourceRefreshSound"] = f"fresh-{index}.wav"
        source_before = copy.deepcopy(refreshed)
        bindings = {
            "core:gremlin": {"animation": "NH_CabirCompleteHandoff.def", "map": "NH_CabirHandoffMap.def",
                             "missile": {"projectile": "NH_CabirHandoffFireball.def", "attackClimaxFrame": 4}},
            "core:masterGremlin": {"animation": "NH_CabirMasterCompleteHandoff.def", "map": "NH_CabirMasterHandoffMap.def",
                                   "missile": {"projectile": "NH_CabirHandoffFireball.def", "attackClimaxFrame": 4}},
            "core:mage": {"animation": "magi-vcmi-complete/magi/NH_CMAGE", "missile": {"projectile": "magi-vcmi-complete/projectile/NH_PMAGEX"}},
            "core:archMage": {"animation": "magi-vcmi-complete/archmagi/NH_CAMAGE", "missile": {"attackClimaxFrame": 8, "ray": [1, 2, 3, 4, 5]}},
        }
        cabir_patch = {"creatures": {creature_id: {"set": bindings[creature_id], "remove": []}
                                    for creature_id in creature_ids[:2]}}
        magi_patch = {"creatures": {creature_id: {"set": bindings[creature_id],
                                    "remove": ["map", "mapMask", "mapAttackFromLeft", "mapAttackFromRight"]
                                    if creature_id == "core:archMage" else []}
                                   for creature_id in creature_ids[2:]}}
        result = module.merge_graphics(refreshed, [cabir_patch, magi_patch])
        self.assertEqual(set(result), set(refreshed))
        for creature_id, source_creature in refreshed.items():
            with self.subTest(creature=creature_id):
                self.assertEqual({key: value for key, value in result[creature_id].items() if key != "graphics"},
                                 {key: value for key, value in source_creature.items() if key != "graphics"})
                if creature_id not in creature_ids:
                    self.assertEqual(result[creature_id], source_creature)
        for patch in (cabir_patch, magi_patch):
            for creature_id, change in patch["creatures"].items():
                with self.subTest(binding=creature_id):
                    graphics = result[creature_id]["graphics"]
                    for field in change["remove"]:
                        self.assertNotIn(field, graphics)
                    for field, value in change["set"].items():
                        if isinstance(value, dict):
                            for nested_field, nested_value in value.items():
                                self.assertEqual(graphics[field][nested_field], nested_value)
                        else:
                            self.assertEqual(graphics[field], value)
                    for field, value in source_before[creature_id]["graphics"].get("missile", {}).items():
                        if field not in change["set"].get("missile", {}):
                            self.assertEqual(graphics["missile"][field], value)
        self.assertEqual(refreshed, source_before)

    def test_rejects_live_repository_payload(self):
        with self.assertRaises(ValueError):
            module.checked_target(path.parents[2])

    def test_preserves_gameplay_and_inherited_missile_fields(self):
        baseline = {"core:mage": {"attack": 5, "abilities": {"shooter": True},
                    "graphics": {"map": "old", "missile": {"offset": {"x": 1}, "frameAngles": [0]}}}}
        patch = {"creatures": {"core:mage": {"remove": ["map"],
                 "set": {"animation": "new", "missile": {"projectile": "red"}}}}}
        result = module.merge_graphics(baseline, [patch])
        self.assertEqual(result["core:mage"]["attack"], 5)
        self.assertEqual(result["core:mage"]["abilities"], baseline["core:mage"]["abilities"])
        self.assertNotIn("map", result["core:mage"]["graphics"])
        self.assertEqual(result["core:mage"]["graphics"]["missile"]["frameAngles"], [0])
        self.assertEqual(baseline["core:mage"]["graphics"]["map"], "old")

    def test_rejects_duplicate_ownership(self):
        patch = {"creatures": {"a": {"set": {}, "remove": []}}}
        with self.assertRaises(ValueError):
            module.merge_graphics({"a": {}}, [patch, patch])

    def test_rejects_non_graphics_payload(self):
        with self.assertRaises(ValueError):
            module.merge_graphics({"a": {}}, [{"creatures": {}, "stats": {}}])

    def test_rejects_unknown_creature(self):
        with self.assertRaises(ValueError):
            module.merge_graphics({}, [{"creatures": {"unknown": {"set": {}, "remove": []}}}])


if __name__ == "__main__":
    unittest.main()
