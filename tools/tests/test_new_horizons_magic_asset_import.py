#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic safety/geometry guards; no purchaser artwork fixture."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from unittest import mock

from PIL import Image, ImageChops, ImageDraw

SPEC = importlib.util.spec_from_file_location(
    "magic_asset_import", Path(__file__).parents[1] / "import_new_horizons_magic_assets.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


def png_bytes(image):
    stream = io.BytesIO()
    image.save(stream, format="PNG")
    return stream.getvalue()


def synthetic_guild_inputs():
    background = Image.new("RGB", (800, 600), (46, 58, 72))
    draw = ImageDraw.Draw(background)
    x, y = 378, 344
    for row in range(43):
        if row == 0:
            left, width = 15, 16
        elif row <= 20:
            left, width = 18, 10
        else:
            left, width = 18, 9
        draw.line((x + left, y + 24 + row, x + left + width - 1, y + 24 + row), fill=(240, 12, 8))
    clean_plate = Image.new("RGB", (1448, 1086), (29, 91, 62))
    return background, clean_plate


def synthetic_archive(path):
    files = {}
    checksums = {}

    def add(name, data):
        files[name] = data
        checksums[name] = IMPORTER.sha(data)

    casting = png_bytes(Image.new("RGBA", (8, 8), (20, 30, 40, 200)))
    for sprite in range(18):
        for school in IMPORTER.SCHOOLS:
            for frame in range(8):
                add(f"{IMPORTER.CAST_ROOT}sprites/CH{sprite + 1:03}/{school}/glow_overlays/frame_{frame:02}.png", casting)

    for index, faction in enumerate(IMPORTER.FACTIONS):
        cutout = Image.new("RGBA", IMPORTER.GUILD_BOOK_SIZE, (0, 0, 0, 0))
        draw = ImageDraw.Draw(cutout)
        color = (50 + index * 10, 70, 90 + index * 8, 255)
        draw.rectangle((15, 25, 106, 135), fill=color)
        draw.rectangle((23, 5, 29, 125), fill=(225, 180, 30, 255))
        draw.rectangle((87, 10, 93, 137), fill=(40, 80, 220, 255))
        alpha = cutout.getchannel("A")
        add(f"mage-guild-bookmarks/transparent/{faction}.png", png_bytes(cutout))
        add(f"mage-guild-bookmarks/masks/{faction}.png", png_bytes(alpha))
        add(f"mage-guild-bookmarks/{faction}.png", png_bytes(cutout.convert("RGB")))

    book_manifest = {
        "preview_size": [1448, 1086],
        "native_guild_size": [800, 600],
        "crop_xywh": [684, 623, 122, 153],
    }
    add("mage-guild-bookmarks/manifest.json", json.dumps(book_manifest).encode())
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr("collection-manifest.json", json.dumps({
            "casting_version": 6,
            "files_sha256": checksums,
        }))
        for name, data in files.items():
            bundle.writestr(name, data)
    return files


class MagicAssetImportTest(unittest.TestCase):
    def test_shared_native_endpoints(self):
        self.assertEqual(IMPORTER.native_rect([684, 623, 122, 153]), (378, 344, 67, 85))

    def test_alpha_is_required_for_casting(self):
        stream = io.BytesIO()
        Image.new("RGB", (8, 8)).save(stream, format="PNG")
        with self.assertRaisesRegex(ValueError, "RGBA"):
            IMPORTER.png(stream.getvalue(), require_alpha=True)

    def test_native_dimensions_must_match(self):
        stream = io.BytesIO()
        Image.new("RGBA", (8, 8)).save(stream, format="PNG")
        with self.assertRaisesRegex(ValueError, "dimensions"):
            IMPORTER.png(stream.getvalue(), (10, 10), True)

    def test_archive_traversal_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "fixture.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("../escaped.png", b"no pixels")
                bundle.writestr("collection-manifest.json", json.dumps({"casting_version": 6}))
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                IMPORTER.prepare(archive)
            self.assertFalse((Path(directory) / "escaped.png").exists())

    def test_duplicate_entries_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "fixture.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("one", "first")
                with self.assertWarns(UserWarning):
                    bundle.writestr("one", "second")
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                IMPORTER.prepare(archive)

    def test_clean_plate_mode_requires_both_pinned_inputs(self):
        with self.assertRaisesRegex(ValueError, "both private input images"):
            IMPORTER.prepare("unused.zip", guild_clean_plate="unused.png")

    def test_clean_plate_requires_pinned_image_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "not-pinned.png"
            path.write_bytes(png_bytes(Image.new("RGB", (1448, 1086), (1, 2, 3))))
            with self.assertRaisesRegex(ValueError, "Unexpected private guild clean plate SHA-256"):
                IMPORTER._load_private_guild_png(
                    path, IMPORTER.GUILD_CLEAN_PLATE_SHA256, (1448, 1086), "guild clean plate")

    def test_clean_plate_composite_is_a_native_sparse_overlay(self):
        background, clean_plate = synthetic_guild_inputs()
        x, y, width, height = IMPORTER.native_rect([684, 623, 122, 153])
        restore_mask = IMPORTER._guild_ribbon_restore_mask(background, (x, y, width, height))
        cutout = Image.new("RGBA", IMPORTER.GUILD_BOOK_SIZE, (0, 0, 0, 0))
        ImageDraw.Draw(cutout).rectangle((23, 5, 50, 137), fill=(220, 90, 30, 255))
        clean_native = clean_plate.resize((800, 600), Image.Resampling.LANCZOS)
        overlay = IMPORTER._compose_clean_plate_book(
            background, clean_native, restore_mask, cutout, (x, y, width, height))

        self.assertEqual(overlay.size, (67, 85))
        self.assertEqual(overlay.mode, "RGBA")
        local_restore = restore_mask.crop((x, y, x + width, y + height))
        cutout_alpha = cutout.convert("RGBa").resize((width, height), Image.Resampling.LANCZOS).convert("RGBA").getchannel("A")
        output_alpha = overlay.getchannel("A")
        for index, alpha in enumerate(output_alpha.get_flattened_data()):
            permitted = local_restore.getpixel((index % width, index // width)) > 0
            permitted = permitted or cutout_alpha.getpixel((index % width, index // width)) > 0
            if not permitted:
                self.assertEqual(alpha, 0)
            if alpha == 0:
                self.assertEqual(overlay.getpixel((index % width, index // width))[:3], (0, 0, 0))

        scene = background.convert("RGBA")
        scene.alpha_composite(overlay, (x, y))
        allowed = Image.new("L", background.size, 0)
        allowed.paste(local_restore, (x, y))
        resized_alpha = cutout.convert("RGBa").resize((width, height), Image.Resampling.LANCZOS).convert("RGBA").getchannel("A")
        allowed.paste(ImageChops.lighter(allowed.crop((x, y, x + width, y + height)), resized_alpha), (x, y))
        for py in range(background.height):
            for px in range(background.width):
                if allowed.getpixel((px, py)) == 0:
                    self.assertEqual(scene.getpixel((px, py))[:3], background.getpixel((px, py)))
        self.assertEqual(overlay.getpixel((29, 30))[:3], (29, 91, 62))

    def test_clean_plate_prepare_keeps_all_nine_private_book_routes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "fixture.zip"
            source_files = synthetic_archive(archive)
            native, clean = synthetic_guild_inputs()
            native_path = root / "native.png"
            clean_path = root / "clean.png"
            native_bytes, clean_bytes = png_bytes(native), png_bytes(clean)
            native_path.write_bytes(native_bytes)
            clean_path.write_bytes(clean_bytes)
            with mock.patch.object(IMPORTER, "GUILD_NATIVE_BACKGROUND_SHA256", IMPORTER.sha(native_bytes)), \
                    mock.patch.object(IMPORTER, "GUILD_CLEAN_PLATE_SHA256", IMPORTER.sha(clean_bytes)):
                outputs = IMPORTER.prepare(archive, clean_path, native_path)

        config = json.loads(outputs["config/newHorizonsMagicAssets.json"])
        self.assertEqual(set(config["guildBooks"]), set(IMPORTER.FACTIONS))
        self.assertTrue(all(value["position"] == [378, 344] for value in config["guildBooks"].values()))
        self.assertEqual(len([name for name in outputs if "/guild/" in name]), 9)
        for faction in IMPORTER.FACTIONS:
            resource = f"{IMPORTER.IMAGE_ROOT}{IMPORTER.RESOURCE_ROOT}guild/{faction}.png"
            image = IMPORTER.png(outputs[resource], (67, 85), require_alpha=True)
            self.assertEqual(image.mode, "RGBA")
            self.assertGreater(image.getchannel("A").getbbox()[2], 0)
        receipt = json.loads(outputs["PRIVATE_MAGIC_ASSETS.json"])
        self.assertTrue(receipt["privateOnly"])
        self.assertFalse(receipt["sourcePixelsCommitted"])
        self.assertEqual(receipt["guildImportMode"], "cleanPlateMasked")
        self.assertEqual(receipt["guildPrivateInputs"]["cleanPlateSha256"], IMPORTER.sha(clean_bytes))
        self.assertEqual(receipt["guildPrivateInputs"]["nativeBackgroundSha256"], IMPORTER.sha(native_bytes))
        self.assertEqual(receipt["guildRestorationMaskNative"]["bbox"], [391, 366, 411, 413])
        self.assertEqual(set(receipt["guildSourceInputs"]), set(IMPORTER.FACTIONS))
        for faction in IMPORTER.FACTIONS:
            self.assertEqual(
                receipt["guildSourceInputs"][faction]["transparentSha256"],
                IMPORTER.sha(source_files[f"mage-guild-bookmarks/transparent/{faction}.png"]))

    def test_default_import_remains_legacy_and_marks_opaque_crop(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "fixture.zip"
            synthetic_archive(archive)
            outputs = IMPORTER.prepare(archive)
        receipt = json.loads(outputs["PRIVATE_MAGIC_ASSETS.json"])
        self.assertEqual(receipt["guildImportMode"], "legacyOpaqueCropNotVisuallyAccepted")
        image = IMPORTER.png(
            outputs[f"{IMPORTER.IMAGE_ROOT}{IMPORTER.RESOURCE_ROOT}guild/tower.png"],
            (67, 85), require_alpha=True)
        self.assertEqual(image.getchannel("A").getextrema(), (255, 255))


if __name__ == "__main__":
    unittest.main()
