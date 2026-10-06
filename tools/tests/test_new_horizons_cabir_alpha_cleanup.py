"""Focused tests for the explicitly reviewed Cabir alpha-cleanup policy."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import clean_new_horizons_cabir_alpha as cleaner
import export_new_horizons_cabir_animation as animation_exporter


def make_grid(with_high_detached_pixel: bool = False) -> Image.Image:
    image = Image.new("RGBA", (25, 25), (77, 88, 99, 0))
    boundaries = (0, 12, 25)
    colors = ((200, 20, 30), (20, 200, 30), (30, 40, 210), (220, 190, 20))
    for row in range(2):
        for column in range(2):
            x0, x1 = boundaries[column], boundaries[column + 1]
            y0, y1 = boundaries[row], boundaries[row + 1]
            color = colors[row * 2 + column]
            for y in range(y0 + 3, y0 + 8):
                for x in range(x0 + 3, x0 + 8):
                    image.putpixel((x, y), (*color, 255))
            image.putpixel((x0 + 8, y0 + 5), (*color, 12))  # keep near-edge AA unchanged
            image.putpixel((x0, y0), (*color, 6))  # detached faint speck to clear
    if with_high_detached_pixel:
        image.putpixel((24, 0), (255, 255, 255, 11))
    return image


class CabirAlphaCleanupTest(unittest.TestCase):
    def test_preserves_retained_rgba_and_only_clears_selected_alpha(self):
        source = make_grid()
        original = source.tobytes()
        cleaned, receipt = cleaner.clean_atlas(source, source_sha256="fixture-sha")

        self.assertEqual(source.tobytes(), original, "cleanup mutated its input image")
        self.assertEqual(cleaned.size, source.size)
        self.assertEqual(receipt["sourceSha256"], "fixture-sha")
        self.assertEqual(receipt["removedPixelCount"], 4)
        self.assertEqual(receipt["maximumRemovedAlpha"], 6)
        self.assertFalse(receipt["sourceFileModified"])
        self.assertEqual(
            [(cell["row"], cell["column"]) for cell in receipt["cells"]],
            [(0, 0), (0, 1), (1, 0), (1, 1)],
        )

        for row in range(2):
            for column in range(2):
                x0, y0 = (0, 12)[column], (0, 12)[row]
                # Core and its two-pixel neighborhood, including alpha-12 AA,
                # retain exact source RGBA; only the detached corner speck loses alpha.
                self.assertEqual(cleaned.getpixel((x0 + 8, y0 + 5)), source.getpixel((x0 + 8, y0 + 5)))
                original_speck = source.getpixel((x0, y0))
                cleaned_speck = cleaned.getpixel((x0, y0))
                self.assertEqual(cleaned_speck[:3], original_speck[:3])
                self.assertEqual(cleaned_speck[3], 0)
                for y in range(y0 + 3, y0 + 8):
                    for x in range(x0 + 3, x0 + 8):
                        self.assertEqual(cleaned.getpixel((x, y)), source.getpixel((x, y)))

        animation_exporter.load_atlas_from_image(cleaned, columns=2, rows=2)

    def test_refuses_to_remove_alpha_above_reviewed_faint_limit(self):
        source = make_grid(with_high_detached_pixel=True)
        before = source.tobytes()
        with self.assertRaisesRegex(ValueError, "above the reviewed limit 10"):
            cleaner.clean_atlas(source)
        self.assertEqual(source.tobytes(), before)

    def test_oversized_header_is_rejected_before_source_pixels_are_copied(self):
        opened = Mock()
        opened.__enter__ = Mock(return_value=opened)
        opened.__exit__ = Mock(return_value=False)
        opened.width = animation_exporter.MAX_SOURCE_SIDE + 1
        opened.height = 4
        opened.mode = "RGBA"
        opened.format = "PNG"
        opened.is_animated = False

        with patch.object(cleaner.Image, "open", return_value=opened):
            with self.assertRaisesRegex(ValueError, "safe source limit"):
                cleaner._read_source_atlas(Path("oversized-header.png"))
        opened.copy.assert_not_called()

    def test_output_symlinks_are_refused_before_any_write(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-cleanup-link-") as temp:
            root = Path(temp)
            target = root / "target"
            symlink = root / "review-bundle"
            symlink.symlink_to(target)

            with self.assertRaisesRegex(FileExistsError, "symlink cleanup output"):
                cleaner.export_review_bundle(cleaner.SOURCE_ATLAS, symlink)

            self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
