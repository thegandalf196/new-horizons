"""Mechanical small-portrait contracts with synthetic, non-proprietary pixels."""
import tempfile
from pathlib import Path
import unittest

from PIL import Image

from tools.export_new_horizons_magi_small_portraits import FORMS, PREFIX, digest, export, portrait_small


class MagiSmallPortraitTest(unittest.TestCase):
    def foreground(self):
        image = Image.new("RGBA", (58, 64))
        image.paste((120, 80, 40, 255), (4, 8, 50, 60))
        image.paste((220, 20, 30, 255), (48, 8, 54, 14))  # upper staff endpoint
        image.paste((220, 210, 150, 255), (20, 8, 28, 16))  # complete head
        return image

    def test_complete_subject_safe_inset_and_native_palette(self):
        source = self.foreground()
        image = portrait_small(source)
        self.assertEqual(image.size, (32, 32))
        bounds = image.getchannel("A").getbbox()
        self.assertTrue(bounds[0] >= 2 and bounds[1] >= 2 and bounds[2] <= 30 and bounds[3] <= 30)
        pixels = lambda i: {i.getpixel((x, y)) for y in range(i.height) for x in range(i.width)}
        self.assertTrue(pixels(image) <= pixels(source))
        self.assertIn((220, 20, 30, 255), pixels(image))
        self.assertIn((220, 210, 150, 255), pixels(image))

    def test_empty_or_wrong_sized_source_rejected(self):
        with self.assertRaises(ValueError):
            portrait_small(Image.new("RGBA", (32, 32)))
        with self.assertRaises(ValueError):
            portrait_small(Image.new("RGBA", (58, 64)))

    def test_semitransparent_subject_alpha_is_not_multiplied(self):
        source = Image.new("RGBA", (58, 64))
        source.paste((90, 120, 160, 128), (10, 10, 38, 38))
        image = portrait_small(source)
        self.assertEqual(image.getchannel("A").getextrema(), (0, 128))
        self.assertEqual(image.getpixel((16, 16)), (90, 120, 160, 128))

    def test_checkout_and_linked_outputs_are_rejected_before_any_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = root / "other-worktree"
            checkout.mkdir()
            (checkout / ".git").write_text("gitdir: outside-metadata\n")
            source = root / "unused-inputs"
            linked = root / "linked"
            external = root / "external"
            external.mkdir()
            linked.symlink_to(external, target_is_directory=True)
            for output, review in ((checkout / "output", None),
                                   (root / "output", checkout / "review"),
                                   (linked / "output", None)):
                with self.subTest(output=output, review=review), self.assertRaises(ValueError):
                    export(source, output, review)
                self.assertFalse(output.exists())
                if review:
                    self.assertFalse(review.exists())

    def test_sparse_existing_bindings_and_source_immutability(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output, review = root / "snapshot", root / "output", root / "review"
            icons = source / PREFIX
            icons.mkdir(parents=True)
            for form in FORMS:
                for suffix, image in (("portrait-foreground-58x64", self.foreground()),
                                      ("portrait-58x64", self.foreground()),
                                      ("small-32", Image.new("RGBA", (32, 32), (20, 30, 40, 255)))):
                    image.save(icons / f"{form}-{suffix}.png")
            before = {str(path): digest(path) for path in icons.glob("*.png")}
            result = export(source, output, review)
            self.assertEqual(before, {str(path): digest(path) for path in icons.glob("*.png")})
            self.assertEqual(len(result["outputs"]), 4)
            for variant in ("nearest", "lanczos"):
                actual = {path.relative_to(output / f"overlay-{variant}") for path in (output / f"overlay-{variant}").rglob("*") if path.is_file()}
                self.assertEqual(actual, {PREFIX / f"{form}-small-32.png" for form in FORMS})
            self.assertEqual(digest(output / "comparison-native.png"), digest(review / "comparison-native.png"))
            with self.assertRaises(ValueError):
                export(source, output, review)


if __name__ == "__main__":
    unittest.main()
