# SPDX-License-Identifier: GPL-2.0-or-later
"""Mechanical checks only; synthetic images, never purchaser resource fixtures."""

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

from PIL import Image


SPEC = importlib.util.spec_from_file_location(
    "wisp_portraits", Path(__file__).resolve().parents[1] / "fit_new_horizons_wisp_portraits.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


def pcx(width=100, height=120):
    palette = bytes([0, 0, 0, 20, 90, 40]) + bytes(762)
    return struct.pack("<III", width * height, width, height) + bytes([1]) * (width * height) + palette


def def_portrait(width, height):
    count = 123
    frame_offset = 784 + 16 + count * 17
    palette = bytes([0, 255, 255, 30, 70, 90]) + bytes(762)
    header = struct.pack("<IIII", 0x47, width, height, 1) + palette
    directory = struct.pack("<IIII", 0, count, 0, 0) + bytes(count * 13)
    directory += struct.pack("<" + "I" * count, *([frame_offset] * count))
    rows = b"".join(bytes([1, width - 1]) for _ in range(height))
    frame = struct.pack("<IIIIIIii", 32 + height * 6, 1, width, height, width, height, 0, 0)
    frame += struct.pack("<" + "I" * height, *(height * 4 + y * 2 for y in range(height))) + rows
    return header + directory + frame


class WispPortraitTests(unittest.TestCase):
    def synthetic_export(self, root):
        images, data, output = root / "images", root / "data", root / "export"
        data.mkdir()
        for archive in ("H3ab_bmp.lod", "H3ab_spr.lod"):
            (data / archive).write_bytes(b"synthetic archive")
        for names in TOOL.FORMS.values():
            for relative, size in zip(names, ((58, 64), (32, 32))):
                path = images / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                image = Image.new("RGBA", size)
                image.putpixel((5, 6), (255, 40, 180, 255))
                image.putpixel((6, 6), (80, 180, 250, 128))
                image.save(path)
        resources = {"TPCASELE.pcx": pcx(), "CRBKGELE.pcx": pcx(100, 130),
                     "TWCRPORT.def": def_portrait(58, 64), "CPRSMALL.def": def_portrait(32, 32)}
        with patch.object(TOOL, "read_lod_entry", side_effect=lambda _, name: resources[name]):
            TOOL.export(images, data, output)
        return output

    @staticmethod
    def file_bytes(root):
        return {path.relative_to(root): path.read_bytes() for path in root.rglob("*") if path.is_file()}

    @staticmethod
    def refresh_output_hash(export_root, name):
        manifest_path = export_root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["outputs"][name]["sha256"] = TOOL.digest((export_root / name).read_bytes())
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    def test_alpha_composition_preserves_source_geometry_and_opaque_pixels(self):
        source = Image.new("RGBA", (3, 1))
        source.putdata([(250, 20, 5, 255), (60, 120, 230, 128), (7, 8, 9, 0)])
        before = source.tobytes()
        backdrop = Image.new("RGBA", source.size, (20, 80, 40, 255))
        result = TOOL.composite_exact(source, backdrop)
        self.assertEqual(result.getpixel((0, 0)), source.getpixel((0, 0)))
        self.assertEqual(result.getpixel((2, 0)), backdrop.getpixel((2, 0)))
        self.assertEqual(result.getpixel((1, 0)), (40, 100, 135, 255))
        self.assertEqual(source.tobytes(), before)
        self.assertEqual(result.size, source.size)

    def test_composite_rejects_empty_or_wrong_native_geometry(self):
        with self.assertRaises(ValueError):
            TOOL.composite_exact(Image.new("RGBA", (3, 2)), Image.new("RGBA", (3, 2)))
        with self.assertRaises(ValueError):
            TOOL.composite_exact(Image.new("RGBA", (3, 2)), Image.new("RGBA", (2, 2)))

    def test_pcx_decodes_actual_palette_and_bgr_byte_order(self):
        self.assertEqual(TOOL.decode_pcx(pcx()).getpixel((0, 0)), (20, 90, 40, 255))
        rgb = struct.pack("<III", 3, 1, 1) + bytes([1, 2, 3])
        self.assertEqual(TOOL.decode_pcx(rgb).getpixel((0, 0)), (3, 2, 1, 255))
        with self.assertRaises(ValueError):
            TOOL.decode_pcx(pcx()[:-1])

    def test_lod_extracts_compressed_resource_without_writing_archive(self):
        payload = pcx()
        compressed = zlib.compress(payload)
        data = bytearray(124)
        data[:3] = b"LOD"
        struct.pack_into("<I", data, 8, 1)
        data[92:108] = b"TPCASELE.pcx".ljust(16, b"\0")
        struct.pack_into("<IIII", data, 108, 124, len(payload), 0, len(compressed))
        data.extend(compressed)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.lod"
            path.write_bytes(data)
            self.assertEqual(TOOL.read_lod_entry(path, "tpcasele.pcx"), payload)
            self.assertEqual(path.read_bytes(), data)
            with self.assertRaises(ValueError):
                TOOL.read_lod_entry(path, "missing.pcx")

    def test_def_reference_decodes_native_frame_and_transparent_small_role(self):
        image = TOOL.decode_portrait_reference(def_portrait(32, 32), 121, True)
        self.assertEqual(image.size, (32, 32))
        self.assertEqual(image.getpixel((0, 0)), (30, 70, 90, 255))
        with self.assertRaises(ValueError):
            TOOL.decode_portrait_reference(def_portrait(32, 32), 124, True)

    def test_export_preserves_small_png_bytes_and_is_non_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            images = root / "images"
            data = root / "data"
            data.mkdir()
            for archive in ("H3ab_bmp.lod", "H3ab_spr.lod"):
                (data / archive).write_bytes(b"synthetic archive")
            inputs = {}
            for names in TOOL.FORMS.values():
                for relative, size in zip(names, ((58, 64), (32, 32))):
                    path = images / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    image = Image.new("RGBA", size)
                    image.putpixel((5, 6), (255, 40, 180, 255))
                    image.putpixel((6, 6), (80, 180, 250, 128))
                    image.save(path)
                    inputs[path] = path.read_bytes()
            resources = {"TPCASELE.pcx": pcx(), "CRBKGELE.pcx": pcx(100, 130),
                         "TWCRPORT.def": def_portrait(58, 64), "CPRSMALL.def": def_portrait(32, 32)}
            output = root / "output"
            with patch.object(TOOL, "read_lod_entry", side_effect=lambda _, name: resources[name]):
                manifest = TOOL.export(images, data, output)
            self.assertTrue(manifest["inputsUnchanged"])
            for form, (_, small) in TOOL.FORMS.items():
                self.assertEqual((output / f"{form}-small.png").read_bytes(), inputs[images / small])
                with Image.open(output / f"{form}-large.png") as image:
                    self.assertEqual(image.size, (58, 64))
                    self.assertEqual(image.getpixel((5, 6)), (255, 40, 180, 255))
                    self.assertEqual(image.getpixel((0, 0)), (20, 90, 40, 255))
            for path, original in inputs.items():
                self.assertEqual(path.read_bytes(), original)
            with self.assertRaises(ValueError):
                TOOL.export(images, data, output)

    def test_stage_runtime_preserves_exact_large_bytes_and_only_existing_bindings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            export_root = self.synthetic_export(root)
            before = self.file_bytes(root)
            overlay = root / "fresh-overlay"
            receipt = TOOL.stage_runtime(export_root, overlay)
            staged_files = set(self.file_bytes(overlay))
            expected = {
                Path("Mods/new-horizons/Images/Wisp/icons/wisp-icon-58x64.png"): "wisp-large.png",
                Path("Mods/new-horizons/Images/WispUpgrade/icons/icon-58x64.png"): "greater-wisp-large.png",
            }
            self.assertEqual(staged_files, set(expected))
            self.assertEqual(receipt["exportManifestSha256"], TOOL.digest((export_root / "manifest.json").read_bytes()))
            for relative, source in expected.items():
                self.assertEqual((overlay / relative).read_bytes(), (export_root / source).read_bytes())
            for relative, original in before.items():
                self.assertEqual((root / relative).read_bytes(), original)
            self.assertFalse(any("32" in str(path) for path in staged_files))
            self.assertFalse(any("battle" in str(path).lower() for path in staged_files))

    def test_stage_runtime_rejects_existing_or_nested_destination_without_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            export_root = self.synthetic_export(root)
            existing = root / "existing-overlay"
            existing.mkdir()
            (existing / "sentinel").write_bytes(b"do not overwrite")
            before = self.file_bytes(root)
            with self.assertRaises(ValueError):
                TOOL.stage_runtime(export_root, existing)
            nested = export_root / "nested-overlay"
            with self.assertRaises(ValueError):
                TOOL.stage_runtime(export_root, nested)
            self.assertFalse(nested.exists())
            self.assertEqual(self.file_bytes(root), before)

    def test_stage_runtime_rejects_stale_hash_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            export_root = self.synthetic_export(root)
            changed = export_root / "greater-wisp-large.png"
            changed.write_bytes(changed.read_bytes() + b"stale synthetic bytes")
            before = self.file_bytes(root)
            overlay = root / "fresh-overlay"
            with self.assertRaises(ValueError):
                TOOL.stage_runtime(export_root, overlay)
            self.assertFalse(overlay.exists())
            self.assertEqual(self.file_bytes(root), before)

    def test_stage_runtime_rejects_malformed_manifest_binding_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            export_root = self.synthetic_export(root)
            manifest_path = export_root / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["outputs"]["greater-wisp-large.png"]["source"] = "WispUpgrade/icons/icon-32x32.png"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            before = self.file_bytes(root)
            overlay = root / "fresh-overlay"
            with self.assertRaises(ValueError):
                TOOL.stage_runtime(export_root, overlay)
            self.assertFalse(overlay.exists())
            self.assertEqual(self.file_bytes(root), before)

    def test_stage_runtime_rejects_wrong_native_geometry_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            export_root = self.synthetic_export(root)
            name = "greater-wisp-large.png"
            Image.new("RGBA", (57, 64), (10, 20, 30, 255)).save(export_root / name)
            self.refresh_output_hash(export_root, name)
            before = self.file_bytes(root)
            overlay = root / "fresh-overlay"
            with self.assertRaises(ValueError):
                TOOL.stage_runtime(export_root, overlay)
            self.assertFalse(overlay.exists())
            self.assertEqual(self.file_bytes(root), before)

    def test_stage_runtime_rejects_nonopaque_large_portrait_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            export_root = self.synthetic_export(root)
            name = "greater-wisp-large.png"
            image = Image.new("RGBA", (58, 64), (10, 20, 30, 255))
            image.putpixel((0, 0), (10, 20, 30, 254))
            image.save(export_root / name)
            self.refresh_output_hash(export_root, name)
            before = self.file_bytes(root)
            overlay = root / "fresh-overlay"
            with self.assertRaises(ValueError):
                TOOL.stage_runtime(export_root, overlay)
            self.assertFalse(overlay.exists())
            self.assertEqual(self.file_bytes(root), before)


if __name__ == "__main__":
    unittest.main()
