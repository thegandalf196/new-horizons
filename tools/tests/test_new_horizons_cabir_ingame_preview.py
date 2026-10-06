"""Validate the detached static Cabir battle-preview bundle and loader groups."""

import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_cabir_ingame_preview as preview


class CabirIngamePreviewTest(unittest.TestCase):
    def test_descriptor_covers_every_creature_animation_group(self):
        descriptor = preview.expected_animation_descriptor()
        groups = [sequence["group"] for sequence in descriptor["sequences"]]
        constants = (ROOT / "client/battle/BattleConstants.h").read_text(encoding="utf-8")
        match = re.search(r"enum class ECreatureAnimType\s*\{(.*?)\};", constants, re.S)
        self.assertIsNotNone(match)
        declared = {
            int(value)
            for _name, value in re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)", match.group(1), re.M)
            if value != "-1"
        }
        self.assertEqual(groups, sorted(groups))
        self.assertEqual(set(groups), declared)
        self.assertEqual(len(groups), 32)
        self.assertTrue(all(sequence["frames"] == ["00.png"] for sequence in descriptor["sequences"]))
        self.assertIn(2, groups, "CreatureAnimation requires a Holding group")
        self.assertIn(5, groups, "CreatureAnimation requires a Death group")

        loader = (ROOT / "clientsdl2/render/RenderHandler.cpp").read_text(encoding="utf-8")
        self.assertIn('group["group"].Integer()', loader)
        self.assertIn('group["frames"].Vector()', loader)

    def test_bundle_keeps_gameplay_unchanged_and_places_approved_sprite(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-preview-test-") as temp:
            output_dir = Path(temp) / "overlay"
            preview.write_bundle(output_dir)
            verified = preview.verify_bundle(output_dir)
            self.assertEqual(verified, output_dir.resolve())

            patch = json.loads((output_dir / preview.CREATURES_PATH).read_text(encoding="utf-8"))
            self.assertEqual(set(patch), {"core:gremlin", "core:masterGremlin"})
            for creature in patch.values():
                self.assertEqual(creature, {"graphics": {"animation": preview.ANIMATION_NAME}})

            with Image.open(preview.SOURCE) as opened:
                source = opened.convert("RGBA")
            expected = Image.new("RGBA", preview.LOGICAL_CANVAS, (0, 0, 0, 0))
            expected.alpha_composite(source, preview.SOURCE_PLACEMENT)
            with Image.open(output_dir / preview.NATIVE_IMAGE) as opened:
                actual = opened.convert("RGBA")
            self.assertEqual(actual.size, (450, 400))
            self.assertEqual(actual.tobytes(), expected.tobytes())
            self.assertEqual(actual.getchannel("A").getbbox(), (169, 211, 224, 268))

            readme = (output_dir / "PREVIEW_README.md").read_text(encoding="utf-8")
            self.assertIn("disposable copy", readme)
            self.assertIn("corpse pose remains standing", readme)
            self.assertIn("gameplay rules are unchanged", readme)
            self.assertIn("user-approved v1 base Cabir standing sprite", readme)

            # Preserve geometry while altering transparent RGB bytes; verify must
            # still reject any pixel-level deviation from the approved export.
            altered = actual.copy()
            altered.putpixel((0, 0), (1, 0, 0, 0))
            altered.save(output_dir / preview.NATIVE_IMAGE)
            with self.assertRaisesRegex(ValueError, "pixels differ"):
                preview.verify_bundle(output_dir)

    def test_v2_bundle_uses_pinned_rougher_draft_with_same_center_and_feet_baseline(self):
        spec = preview.source_spec("v2")
        self.assertEqual(spec["source"], preview.V2_SOURCE)
        self.assertEqual(spec["master"], preview.V2_MASTER)
        self.assertEqual(spec["placement"], (164, 208))
        self.assertEqual(spec["canvas_bounds"], (169, 208, 224, 268))

        with tempfile.TemporaryDirectory(prefix="nh-cabir-preview-v2-test-") as temp:
            output_dir = Path(temp) / "overlay"
            preview.write_bundle(output_dir, version="v2")
            self.assertEqual(preview.verify_bundle(output_dir, version="v2"), output_dir.resolve())

            patch = json.loads((output_dir / preview.CREATURES_PATH).read_text(encoding="utf-8"))
            self.assertEqual(set(patch), {"core:gremlin", "core:masterGremlin"})
            self.assertTrue(
                all(
                    creature == {"graphics": {"animation": preview.ANIMATION_NAME}}
                    for creature in patch.values()
                )
            )

            descriptor = json.loads((output_dir / preview.ANIMATION_DESCRIPTOR).read_text(encoding="utf-8"))
            self.assertEqual(len(descriptor["sequences"]), 32)
            self.assertTrue(all(sequence["frames"] == ["00.png"] for sequence in descriptor["sequences"]))

            with Image.open(preview.V2_SOURCE) as opened:
                source = opened.convert("RGBA")
            expected = Image.new("RGBA", preview.LOGICAL_CANVAS, (0, 0, 0, 0))
            expected.alpha_composite(source, spec["placement"])
            with Image.open(output_dir / preview.NATIVE_IMAGE) as opened:
                actual = opened.convert("RGBA")
            self.assertEqual(actual.tobytes(), expected.tobytes())
            self.assertEqual(actual.getchannel("A").getbbox(), spec["canvas_bounds"])
            self.assertEqual((169 + 224) / 2, 196.5)
            self.assertEqual(spec["canvas_bounds"][3], 268)

            readme = (output_dir / "PREVIEW_README.md").read_text(encoding="utf-8")
            self.assertIn("unapproved rougher/darker v2 draft for static preview only", readme)
            self.assertIn("not approved for normal New Horizons bindings", readme)
            self.assertIn("the live/normal module stays untouched", readme)
            self.assertIn("Every one of the 32 declared creature animation groups", readme)
            self.assertIn("not a complete creature animation set", readme)

    def test_unknown_version_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown Cabir preview version"):
            preview.create_native_frame("v99")

    def test_generator_refuses_live_module_and_existing_output_paths(self):
        with self.assertRaises(ValueError):
            preview.write_bundle(ROOT / "Mods/new-horizons", version="v2")
        with self.assertRaises(ValueError):
            preview.write_bundle(ROOT / "assets/new-horizons/creatures/cabir/preview-copy")

        with tempfile.TemporaryDirectory(prefix="nh-cabir-preview-existing-") as temp:
            output_dir = Path(temp) / "existing"
            output_dir.mkdir()
            marker = output_dir / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                preview.write_bundle(output_dir)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")


if __name__ == "__main__":
    unittest.main()
