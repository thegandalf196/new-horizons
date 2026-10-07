"""Focused non-destructive staging checks for approved base Cabir fidget."""

from pathlib import Path
import hashlib
import io
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import install_new_horizons_cabir_fidget as installer


class CabirFidgetTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cabir-fidget-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.approved = self.root / "approved"
        self.output = self.root / "output"
        self.approved.mkdir()
        self.descriptor = {"basepath": installer.EXPECTED_BASEPATH,
                           "sequences": [{"group": 2, "generateOverlay": 1, "frames": ["idle/frame-00.png"]},
                                         {"group": 0, "frames": ["walk/frame-00.png"]}],
                           "preservedMetadata": "original"}
        descriptor = self.source / installer.DESCRIPTOR
        descriptor.parent.mkdir(parents=True)
        descriptor.write_text(json.dumps(self.descriptor))
        for folder in ("idle", "walk"):
            path = self.source / installer.IMAGE_ROOT / installer.EXPECTED_BASEPATH / folder / "frame-00.png"
            path.parent.mkdir(parents=True)
            path.write_bytes(b"untouched original payload")
        self.hashes = []
        for index in range(4):
            image = Image.new("RGBA", (450, 400))
            ImageDraw.Draw(image).rectangle((180 + index, 210, 209, 266), fill=(150 + index, 80, 30, 255))
            stream = io.BytesIO()
            image.save(stream, format="PNG")
            data = stream.getvalue()
            (self.approved / f"frame-{index:02d}.png").write_bytes(data)
            self.hashes.append(hashlib.sha256(data).hexdigest())
        self.before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.pins = patch.object(installer, "FRAME_HASHES", tuple(self.hashes))
        self.pins.start()

    def tearDown(self):
        self.pins.stop()
        self.temp.cleanup()

    def test_existing_groups_inputs_and_frames_preserved_with_idempotent_output(self):
        first = installer.stage(self.source, self.approved, self.output)
        generated = json.loads((self.output / installer.DESCRIPTOR).read_text())
        self.assertEqual(generated["sequences"][:-1], self.descriptor["sequences"])
        self.assertEqual(generated["preservedMetadata"], "original")
        self.assertEqual(generated["sequences"][-1]["group"], 1)
        self.assertEqual(generated["sequences"][-1]["generateOverlay"], 1)
        for index, frame in enumerate(generated["sequences"][-1]["frames"]):
            self.assertEqual((self.output / installer.IMAGE_ROOT / installer.EXPECTED_BASEPATH / frame).read_bytes(),
                             (self.approved / f"frame-{index:02d}.png").read_bytes())
        times = {p: p.stat().st_mtime_ns for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(installer.stage(self.source, self.approved, self.output), first)
        self.assertEqual(times, {p: p.stat().st_mtime_ns for p in self.output.rglob("*") if p.is_file()})
        for path, data in self.before.items():
            self.assertEqual(path.read_bytes(), data)
        self.assertFalse(any("Master" in p.name for p in self.output.rglob("*")))

    def test_reject_changed_approved_frame_before_output(self):
        (self.approved / "frame-00.png").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            installer.stage(self.source, self.approved, self.output)
        self.assertFalse(self.output.exists())

    def test_reject_displaced_feet_without_repairing_pixels(self):
        path = self.approved / "frame-00.png"
        image = Image.new("RGBA", (450, 400))
        ImageDraw.Draw(image).rectangle((180, 210, 209, 270), fill=(150, 80, 30, 255))
        image.save(path)
        replacement = (hashlib.sha256(path.read_bytes()).hexdigest(), *self.hashes[1:])
        with patch.object(installer, "FRAME_HASHES", replacement):
            with self.assertRaisesRegex(ValueError, "baseline267"):
                installer.stage(self.source, self.approved, self.output)
        self.assertFalse(self.output.exists())

    def test_reject_output_drift_without_overwrite(self):
        installer.stage(self.source, self.approved, self.output)
        path = self.output / installer.DESCRIPTOR
        path.write_bytes(b"foreign")
        with self.assertRaisesRegex(ValueError, "existing overlay differs"):
            installer.stage(self.source, self.approved, self.output)
        self.assertEqual(path.read_bytes(), b"foreign")

    def test_reject_existing_group1_and_unsafe_original_frame(self):
        path = self.source / installer.DESCRIPTOR
        changed = dict(self.descriptor)
        changed["sequences"] = self.descriptor["sequences"] + [{"group": 1, "frames": []}]
        path.write_text(json.dumps(changed))
        with self.assertRaisesRegex(ValueError, "already contains group1"):
            installer.stage(self.source, self.approved, self.output)
        changed["sequences"] = [{"group": 2, "frames": ["../outside.png"]}]
        path.write_text(json.dumps(changed))
        with self.assertRaisesRegex(ValueError, "unsafe frame reference"):
            installer.stage(self.source, self.approved, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
