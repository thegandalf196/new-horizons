# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused geometry and preservation gates for map-only Cabir derivatives."""

import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_cabir_handoff_map as exporter


class CabirHandoffMapTest(unittest.TestCase):
    def test_reference_format_three_ignores_shadows_and_extracts_native_foot(self):
        data = bytearray(832)
        struct.pack_into("<I", data, 12, 1)
        struct.pack_into("<II", data, 784, 0, 8)
        data.extend(b"\0" * 104)
        frame = len(data)
        for index in range(8):
            struct.pack_into("<I", data, 904 + index * 4, frame)
        # Two rows, first:64 shadow pixels; second:32transparent,32body.
        rows = bytes([63, 63, 31, 255]) + bytes([8] * 32)
        header = struct.pack("<IIIIIIii", 0, 3, 64, 64, 64, 2, 0, 60)
        data.extend(header + struct.pack("<HHHH", 8, 8, 10, 10) + rows)
        records = exporter.reference_geometry(bytes(data))
        self.assertEqual(len(records), 8)
        self.assertEqual(records[0]["bodyBounds"], [32, 61, 64, 62])
        self.assertEqual(records[0]["foot"], [48, 62])

    def test_fixed_transform_preserves_walk_displacement_and_both_form_coordinates(self):
        first = Image.new("RGBA", (128, 112))
        for x in range(34, 85):
            for y in range(27, 86):
                first.putpixel((x, y), (160, 30, 10, 255))
        second = Image.new("RGBA", (128, 112))
        second.alpha_composite(first, (3, 0))
        originals = [first, second] * 7
        outputs, transform = exporter.fit_frames(originals, {"targetBodyHeight": 44, "targetFoot": [43, 62]})
        self.assertAlmostEqual(transform["uniformScale"], 44 / 59)
        self.assertEqual(transform["sourceUnion"], [34, 27, 88, 86])
        self.assertEqual(outputs[0].size, (64, 64))
        self.assertIsNone(ImageChops.difference(outputs[0], outputs[2]).getbbox())
        solid = lambda image: image.getchannel("A").point(lambda value: 255 if value >= 128 else 0).getbbox()
        self.assertGreater(solid(outputs[1])[0], solid(outputs[0])[0])
        for image in outputs:
            bounds = image.getchannel("A").getbbox()
            self.assertGreater(bounds[0], 0)
            self.assertGreater(bounds[1], 0)
            self.assertLess(bounds[2], 64)
            self.assertLess(bounds[3], 64)

    def test_off_canvas_transform_refuses_visible_crop(self):
        image = Image.new("RGBA", (128, 112), (100, 40, 20, 255))
        with self.assertRaisesRegex(ValueError, "clip"):
            exporter.fit_frames([image], {"targetBodyHeight": 64, "targetFoot": [43, 62]})

    def test_export_pins_inputs_preserves_other_roles_and_uses_separate_descriptors(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            pins = copy.deepcopy(exporter.PINS)
            before = {}
            for form in pins:
                for index in range(7):
                    path = source / f"cabir-handoff/{form}/map/walk/frame-{index:02}.png"
                    path.parent.mkdir(parents=True, exist_ok=True)
                    image = Image.new("RGBA", (128, 112))
                    image.paste((180, index * 10, 5, 255), (35 + index % 2, 27, 85, 86))
                    image.save(path)
                    before[path] = path.read_bytes()
                    pins[form][index] = exporter.digest(before[path])
            unrelated = source / "battle.png"
            unrelated.write_bytes(b"untouched")
            contract = {"targetBodyHeight": 44, "targetFoot": [43, 62]}
            output, review = root / "overlay", root / "review"
            with patch.object(exporter, "PINS", pins), patch.object(exporter, "native_contract", return_value=contract):
                manifest = exporter.export(source, root / "unused.lod", output, review)
            self.assertTrue(all(path.read_bytes() == data for path, data in before.items()))
            self.assertEqual(unrelated.read_bytes(), b"untouched")
            self.assertEqual(len(manifest["files"]), 16)
            self.assertFalse(any("battle" in name or "encounter" in name for name in manifest["files"]))
            for form in pins:
                descriptor = output / "Mods/new-horizons/Content/sprites" / exporter.DESCRIPTORS[form]
                config = json.loads(descriptor.read_text())
                self.assertEqual(len(config["sequences"][0]["frames"]), 7)
                for relative in config["sequences"][0]["frames"]:
                    path = output / "Mods/new-horizons/Images" / config["basepath"] / relative
                    with Image.open(path) as image:
                        self.assertEqual(image.size, (64, 64))
            for binding in manifest["mapOnlyBindings"].values():
                self.assertEqual(set(binding["graphics"]), {"map"})
            with Image.open(review / "cabir-walk-1x.gif") as gif:
                self.assertEqual(gif.size, (64, 64))
            self.assertEqual((output / "manifest.json").read_bytes(), (review / "manifest.json").read_bytes())
            with patch.object(exporter, "PINS", pins):
                next(iter(before)).write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "pin mismatch"):
                    exporter.export(source, root / "unused.lod", root / "new", root / "new-review")


if __name__ == "__main__":
    unittest.main()
