"""Verify mechanical portrait composition and sparse overlay preservation."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import fit_new_horizons_magi_portraits as fitter


class MagiPortraitFitTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="magi-fit-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.candidates = self.root / "candidates"
        self.overlay = self.root / "overlay"
        self.backdrop = self.root / "NH_academy_creature_portrait_backdrop.png"
        # Nonuniform backdrop makes accidental background scaling detectable.
        image = Image.new("RGBA", (58, 64))
        for y in range(64):
            for x in range(58):
                image.putpixel((x, y), (x * 3, y * 3, (x + y) * 2, 255))
        image.save(self.backdrop)
        for form in fitter.FORMS:
            icons = self.source / "icons"
            icons.mkdir(exist_ok=True, parents=True)
            foreground = Image.new("RGBA", (58, 64))
            ImageDraw.Draw(foreground).rectangle((8, 0, 44, 63), fill=(170, 40, 20, 255))
            foreground.save(icons / f"{form}-portrait-foreground-58x64.png")
            Image.alpha_composite(image, foreground).save(icons / f"{form}-portrait-58x64.png")
            small = Image.new("RGBA", (32, 32))
            ImageDraw.Draw(small).rectangle((0, 0, 24, 31), fill=(180, 70, 30, 255))
            small.save(icons / f"{form}-small-32.png")
            idle = self.source / "battle" / form / "idle"
            idle.mkdir(parents=True)
            standing = Image.new("RGBA", (450, 400))
            ImageDraw.Draw(standing).rectangle((180, 175, 209, 266), fill=(180, 70, 30, 255))
            standing.save(idle / "000.png")
        self.before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        fitter.export_candidates(self.source, self.backdrop, self.candidates)

    def tearDown(self):
        self.temp.cleanup()

    def test_selected_bindings_background_margins_and_idempotent_bytes(self):
        receipt = fitter.stage_selected(self.candidates, self.overlay)
        prefix = self.overlay / "Mods/new-horizons/Images/magi-vcmi-complete/icons"
        self.assertEqual(len(receipt["outputs"]), 6)
        backdrop = Image.open(self.backdrop).convert("RGBA")
        for form in fitter.FORMS:
            large = Image.open(prefix / f"{form}-portrait-58x64.png").convert("RGBA")
            fg = Image.open(prefix / f"{form}-portrait-foreground-58x64.png").convert("RGBA")
            small = Image.open(prefix / f"{form}-small-32.png").convert("RGBA")
            self.assertEqual(large.tobytes(), Image.alpha_composite(backdrop, fg).tobytes())
            for y in range(64):
                for x in range(58):
                    if fg.getpixel((x, y))[3] == 0:
                        self.assertEqual(large.getpixel((x, y)), backdrop.getpixel((x, y)))
            self.assertEqual((prefix / f"{form}-small-32.png").read_bytes(),
                             (self.candidates / f"{form}-original-small-inset.png").read_bytes())
            for image, inset in ((fg, 3), (small, 2)):
                bounds = image.getchannel("A").getbbox()
                self.assertGreaterEqual(bounds[0], inset)
                self.assertGreaterEqual(bounds[1], inset)
                self.assertLessEqual(bounds[2], image.width - inset)
                self.assertLessEqual(bounds[3], image.height - inset)
        self.assertFalse(any(p.suffix == ".json" and p.name != "manifest.json" for p in self.overlay.rglob("*")))
        times = {p: p.stat().st_mtime_ns for p in self.overlay.rglob("*") if p.is_file()}
        self.assertEqual(fitter.stage_selected(self.candidates, self.overlay), receipt)
        self.assertEqual(times, {p: p.stat().st_mtime_ns for p in self.overlay.rglob("*") if p.is_file()})
        for path, data in self.before.items():
            self.assertEqual(path.read_bytes(), data)

    def test_reject_modified_candidate_even_with_updated_local_hash(self):
        path = self.candidates / "magi-authored-large.png"
        image = Image.open(path).convert("RGBA")
        image.putpixel((0, 0), (255, 0, 255, 255))
        image.save(path)
        manifest_path = self.candidates / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["outputs"][path.name]["sha256"] = fitter.sha256(path)
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "not the selected mechanical composition"):
            fitter.stage_selected(self.candidates, self.overlay)
        self.assertFalse(self.overlay.exists())

    def test_reject_original_input_change_and_overlay_drift(self):
        fitter.stage_selected(self.candidates, self.overlay)
        path = self.overlay / "manifest.json"
        path.write_bytes(b"foreign")
        with self.assertRaisesRegex(ValueError, "existing overlay differs"):
            fitter.stage_selected(self.candidates, self.overlay)
        self.assertEqual(path.read_bytes(), b"foreign")
        self.backdrop.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "approved input changed"):
            fitter.stage_selected(self.candidates, self.root / "other")
        self.assertFalse((self.root / "other").exists())


if __name__ == "__main__":
    unittest.main()
