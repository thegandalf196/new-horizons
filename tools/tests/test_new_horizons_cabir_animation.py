"""Tests for deterministic, read-only-source Cabir atlas export."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_cabir_animation as exporter


def make_atlas() -> Image.Image:
    """Four deliberately unequal cels, including preserved semitransparency."""
    atlas = Image.new("RGBA", (48, 12), (11, 22, 33, 0))
    bounds = ((2, 2, 5, 9), (3, 1, 7, 8), (1, 3, 6, 11), (2, 2, 8, 10))
    colors = ((220, 30, 20, 255), (25, 210, 30, 255), (20, 45, 220, 255), (230, 190, 30, 255))
    for frame_index, (box, color) in enumerate(zip(bounds, colors)):
        x0, y0, x1, y1 = box
        offset = frame_index * 12
        for y in range(y0, y1):
            for x in range(x0, x1):
                atlas.putpixel((offset + x, y), color)
        atlas.putpixel((offset + x0 + 1, y0 + 1), (color[0], color[1], color[2], 96))
    return atlas


class CabirAnimationExporterTest(unittest.TestCase):
    def test_shared_extent_and_scale_preserve_distinct_cel_geometry_and_anchor(self):
        atlas = make_atlas()
        frames, info = exporter.render_frames(atlas, columns=4, height=20)

        self.assertEqual(len(frames), 4)
        self.assertEqual(info["sharedSourceBounds"], [1, 1, 8, 11])
        self.assertEqual(info["sharedSourceBodySize"], [7, 10])
        self.assertEqual(info["resizedBodySize"], [14, 20])
        self.assertEqual(info["scale"], 2.0)
        self.assertEqual(info["placement"], [189, 248])
        self.assertEqual(info["anchorXActual"], 196.0)  # even raster width rounds by 0.5px
        self.assertEqual(info["feetBaselineY"], 268)

        for index, frame in enumerate(frames):
            cell = atlas.crop((index * 12, 0, (index + 1) * 12, 12))
            expected_body = cell.crop((1, 1, 8, 11)).resize((14, 20), Image.Resampling.LANCZOS)
            expected = Image.new("RGBA", exporter.LOGICAL_CANVAS, (0, 0, 0, 0))
            expected.alpha_composite(expected_body, (189, 248))
            self.assertEqual(frame.tobytes(), expected.tobytes())
            self.assertEqual(
                frame.getchannel("A").getbbox(),
                expected.getchannel("A").getbbox(),
                f"frame {index} lost its original relative position",
            )

        # Semi-transparent source edges remain alpha, not thresholded pixels.
        self.assertTrue(
            frames[0].getchannel("A").point(lambda value: 255 if 0 < value < 255 else 0).getbbox()
            is not None
        )

    def test_export_outputs_ordered_canvas_frames_nearest_sheet_and_gif(self):
        atlas = make_atlas()
        with tempfile.TemporaryDirectory(prefix="nh-cabir-animation-") as temp:
            root = Path(temp)
            source = root / "atlas.png"
            atlas.save(source)
            original_sha = hashlib.sha256(source.read_bytes()).hexdigest()
            output = root / "walk-export"

            result = exporter.export_animation(source, output)
            self.assertEqual(result, output.resolve())
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), original_sha)
            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                [
                    "animation-proof.gif",
                    "contact-sheet-4x.png",
                    "export.json",
                    "frame-00.png",
                    "frame-01.png",
                    "frame-02.png",
                    "frame-03.png",
                ],
            )

            expected_frames, _ = exporter.render_frames(atlas, columns=4, height=60)
            for index, expected in enumerate(expected_frames):
                with Image.open(output / f"frame-{index:02d}.png") as opened:
                    actual = opened.convert("RGBA")
                self.assertEqual(actual.size, (450, 400))
                self.assertEqual(actual.tobytes(), expected.tobytes())

            with Image.open(output / "contact-sheet-4x.png") as opened:
                contact = opened.convert("RGBA")
            crop_bounds = exporter._review_crop_bounds(expected_frames)
            crop_width = crop_bounds[2] - crop_bounds[0]
            crop_height = crop_bounds[3] - crop_bounds[1]
            self.assertEqual(contact.size, (crop_width * 8, crop_height * 8))
            for index, frame in enumerate(expected_frames):
                nearest = frame.crop(crop_bounds).resize(
                    (crop_width * 4, crop_height * 4), Image.Resampling.NEAREST
                )
                left = (index % 2) * crop_width * 4
                top = (index // 2) * crop_height * 4
                self.assertEqual(
                    contact.crop((left, top, left + crop_width * 4, top + crop_height * 4)).tobytes(),
                    nearest.tobytes(),
                )

            with Image.open(output / "animation-proof.gif") as opened:
                self.assertEqual(opened.n_frames, 4)
                self.assertEqual(opened.size, (crop_width, crop_height))
            metadata = json.loads((output / "export.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["sourceSha256"], original_sha)
            self.assertEqual(metadata["anchorXRequested"], 196.5)
            self.assertEqual(metadata["feetBaselineY"], 268)
            self.assertIn("no thresholding", metadata["alphaPolicy"])

    def test_invalid_atlases_are_refused(self):
        partial = Image.new("RGBA", (15, 12), (0, 0, 0, 0))
        with self.assertRaisesRegex(ValueError, "not divisible"):
            exporter.load_atlas_from_image(partial, columns=4)

        empty_panel = Image.new("RGBA", (48, 12), (0, 0, 0, 0))
        empty_panel.putpixel((2, 2), (255, 0, 0, 255))
        with self.assertRaisesRegex(ValueError, "panel 1 is empty"):
            exporter.load_atlas_from_image(empty_panel, columns=4)

        opaque = Image.new("RGBA", (48, 12), (0, 0, 0, 255))
        with self.assertRaisesRegex(ValueError, "transparent alpha"):
            exporter.load_atlas_from_image(opaque, columns=4)

        no_alpha = Image.new("RGB", (48, 12), (0, 0, 0))
        with self.assertRaisesRegex(ValueError, "real RGBA"):
            exporter.load_atlas_from_image(no_alpha, columns=4)

        clipped = Image.new("RGBA", (48, 12), (0, 0, 0, 0))
        clipped.putpixel((0, 3), (255, 0, 0, 255))
        clipped.putpixel((0, 4), (255, 0, 0, 255))
        with self.assertRaisesRegex(ValueError, "may be clipped"):
            exporter.load_atlas_from_image(clipped, columns=4)

        pathological = Image.new("RGBA", (exporter.MAX_SOURCE_SIDE + 1, 2), (0, 0, 0, 0))
        with self.assertRaisesRegex(ValueError, "safe source limit"):
            exporter.load_atlas_from_image(pathological, columns=4)

        with tempfile.TemporaryDirectory(prefix="nh-cabir-pathological-atlas-") as temp:
            source_path = Path(temp) / "oversized.png"
            pathological.save(source_path)
            with self.assertRaisesRegex(ValueError, "safe source limit"):
                exporter.load_atlas(source_path, columns=4)

    def test_empty_alpha_atlas_and_height_outside_canvas_are_rejected(self):
        empty = Image.new("RGBA", (48, 12), (0, 0, 0, 0))
        with self.assertRaisesRegex(ValueError, "visible art"):
            exporter.load_atlas_from_image(empty, columns=4)

        with self.assertRaisesRegex(ValueError, "body height"):
            exporter.render_frames(make_atlas(), columns=4, height=269)

    def test_output_must_be_new_and_cannot_replace_source_or_runtime_files(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-animation-guards-") as temp:
            root = Path(temp)
            source = root / "atlas.png"
            make_atlas().save(source)
            source_bytes = source.read_bytes()

            with self.assertRaisesRegex(ValueError, "source atlas parent"):
                exporter.export_animation(source, root)
            existing = root / "existing"
            existing.mkdir()
            marker = existing / "preserve.txt"
            marker.write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(FileExistsError, "must be new"):
                exporter.export_animation(source, existing)
            self.assertEqual(marker.read_text(encoding="utf-8"), "keep")

            protected = exporter.ROOT / "Mods/new-horizons/animation-export-test"
            with self.assertRaisesRegex(ValueError, "runtime/configuration"):
                exporter.validate_new_output_directory(source, protected)

            self.assertEqual(source.read_bytes(), source_bytes)
            self.assertFalse((root / "frame-00.png").exists())


if __name__ == "__main__":
    unittest.main()
