import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / "ci/apply_creature_graphics_handoffs.py"
spec = importlib.util.spec_from_file_location("graphics_handoffs", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GraphicsMergeTests(unittest.TestCase):
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
