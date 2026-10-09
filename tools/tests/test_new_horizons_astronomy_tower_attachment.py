"""Synthetic external-workspace checks; no installed or authoring artwork is modified."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import preview_new_horizons_astronomy_tower_attachment as preview


class AstronomyAttachmentTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nh-astronomy-synthetic-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.workspace = self.root / "input"
        self.images = self.workspace / preview.IMAGE_ROOT
        self.images.mkdir(parents=True)
        landscape = self.images / "NH_academy/town/landscape.png"
        landscape.parent.mkdir(parents=True)
        Image.new("RGBA", (800, 374), (1, 2, 3, 255)).save(landscape)
        self.stage = {"animation": "synthetic_stage", "x": 3, "y": 5, "z": -1}
        self.tower = {"animation": "synthetic_tower", "x": 4, "y": 6, "z": 0}
        for name, color in (("stage", (255, 0, 0, 255)), ("tower", (0, 0, 255, 255))):
            Image.new("RGBA", (2, 2), color).save(self.images / f"{name}.png")
            (self.images / f"synthetic_{name}.json").write_text(
                json.dumps({"images": [{"file": f"{name}.png"}]}), encoding="utf-8")

    def test_explicit_external_workspace_and_new_output_required(self):
        output = self.root / "output"
        self.assertEqual(preview.validate_workspace_and_output(self.workspace, output),
                         (self.workspace.resolve(), output.resolve()))
        self.assertFalse(output.exists())
        for forbidden in (self.workspace / "output", self.root):
            with self.assertRaises(ValueError):
                preview.validate_workspace_and_output(self.workspace, forbidden)
        output.mkdir()
        with self.assertRaises(FileExistsError):
            preview.validate_workspace_and_output(self.workspace, output)

    def test_checkout_outputs_rejected_before_mkdir_or_reading_art(self):
        destination = preview.ROOT / "synthetic-forbidden-preview" / "result.png"
        with self.assertRaisesRegex(ValueError, "outside the checkout"):
            preview.save_preview(destination, Image.new("RGBA", (2, 2)))
        self.assertFalse(destination.parent.exists())
        with patch.object(sys, "argv", ["preview", "--input-workspace", str(self.workspace),
                                       "--output-dir", str(destination.parent)]), \
             patch.object(preview, "effective_structures", side_effect=AssertionError("must not read art")):
            with self.assertRaisesRegex(ValueError, "outside the checkout"):
                preview.main()

    def test_symlink_checkout_and_dangling_output_rejected(self):
        for name, target in (("checkout", preview.ROOT), ("dangling", self.root / "absent")):
            link = self.root / name
            link.symlink_to(target, target_is_directory=True)
            with self.assertRaises((ValueError, FileExistsError)):
                preview.validate_workspace_and_output(self.workspace, link)
            self.assertTrue(link.is_symlink())

    def test_native_canvas_exact_placement_layering_and_overlap_preserved(self):
        before = preview.render_stage(self.stage, self.tower, 4, input_workspace=self.workspace)
        after = preview.render_stage(self.stage, self.tower, 5, input_workspace=self.workspace)
        self.assertEqual(before.size, (800, 374))
        self.assertEqual(before.getpixel((0, 0)), (1, 2, 3, 255))
        self.assertEqual(before.getpixel((3, 5)), (255, 0, 0, 255))
        self.assertEqual(before.getpixel((4, 6)), (0, 0, 255, 255))
        self.assertEqual(after.getpixel((4, 6)), (255, 0, 0, 255))
        self.assertEqual(after.getpixel((5, 6)), (0, 0, 255, 255))
        self.assertEqual(preview.overlap_count(preview.structure_image(self.stage, self.workspace), (3, 5, -1),
                                             preview.structure_image(self.tower, self.workspace), (4, 6, 0)), 1)

    def test_external_preview_write_is_new_and_source_bytes_unchanged(self):
        source = self.images / "tower.png"
        original = source.read_bytes()
        path = self.root / "output/result.png"
        image = preview.render_stage(self.stage, self.tower, 4, input_workspace=self.workspace)
        preview.save_preview(path, image)
        with Image.open(path) as saved:
            self.assertEqual(saved.convert("RGBA").tobytes(), image.tobytes())
        with self.assertRaises(FileExistsError):
            preview.save_preview(path, image)
        self.assertEqual(source.read_bytes(), original)

    def test_cli_does_not_supply_an_implicit_build_output(self):
        with patch.object(sys, "argv", ["preview", "--input-workspace", str(self.workspace)]):
            with self.assertRaises(SystemExit) as error:
                preview.main()
        self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
