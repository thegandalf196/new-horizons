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

from PIL import Image

SPEC = importlib.util.spec_from_file_location(
    "magic_asset_import", Path(__file__).parents[1] / "import_new_horizons_magic_assets.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


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


if __name__ == "__main__":
    unittest.main()
