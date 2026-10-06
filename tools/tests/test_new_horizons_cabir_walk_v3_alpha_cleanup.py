"""Focused checks for the pinned, alpha-only barehanded Cabir walk export."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import clean_new_horizons_cabir_alpha as alpha_cleaner
import clean_new_horizons_cabir_walk_v3_alpha as walk_cleaner


def make_atlas() -> Image.Image:
    """Build four separated cells with faint edge flecks and retained AA."""
    atlas = Image.new("RGBA", (40, 40), (61, 72, 83, 0))
    colors = ((210, 70, 35), (205, 95, 45), (220, 80, 30), (195, 65, 25))
    for index, color in enumerate(colors):
        column = index % 2
        row = index // 2
        x0 = column * 20
        y0 = row * 20
        for y in range(y0 + 4, y0 + 12):
            for x in range(x0 + 4, x0 + 12):
                atlas.putpixel((x, y), (*color, 255))
        # Within the reviewed radius-3 antialias safety region.
        atlas.putpixel((x0 + 14, y0 + 8), (*color, 11))
        # Faint detached specks, including the visible left edge of panel zero.
        atlas.putpixel((x0, y0), (*color, 9))
        atlas.putpixel((x0, y0 + 7), (*color, 10))
    return atlas


class CabirWalkV3AlphaCleanupTest(unittest.TestCase):
    def test_radius_three_clears_only_faint_external_alpha_and_keeps_geometry(self):
        source = make_atlas()
        original = source.tobytes()

        cleaned, receipt = alpha_cleaner.clean_atlas(
            source,
            source_sha256="fixture-source",
            dilation_radius=walk_cleaner.DILATION_RADIUS,
        )

        self.assertEqual(source.tobytes(), original)
        self.assertEqual(cleaned.size, source.size)
        self.assertEqual(receipt["removedPixelCount"], 8)
        self.assertEqual(receipt["maximumRemovedAlpha"], 10)
        self.assertEqual(receipt["removedAlphaHistogram"], {"9": 4, "10": 4})
        self.assertEqual(receipt["policy"]["dilation"], "3-pixel square/Chebyshev-radius dilation within each cell")
        self.assertTrue(receipt["policy"]["noRecoloringOrWarping"])

        for index in range(4):
            column = index % 2
            row = index // 2
            x0 = column * 20
            y0 = row * 20
            faint_corner = source.getpixel((x0, y0))
            cleaned_corner = cleaned.getpixel((x0, y0))
            self.assertEqual(cleaned_corner[:3], faint_corner[:3])
            self.assertEqual(cleaned_corner[3], 0)
            self.assertEqual(cleaned.getpixel((x0 + 14, y0 + 8)), source.getpixel((x0 + 14, y0 + 8)))
            self.assertEqual(receipt["cells"][index]["cleanedEdgeAlphaCounts"], {
                "left": 0, "right": 0, "top": 0, "bottom": 0,
            })

        source_bytes = source.tobytes()
        cleaned_bytes = cleaned.tobytes()
        for offset in range(0, len(source_bytes), 4):
            before = source_bytes[offset:offset + 4]
            after = cleaned_bytes[offset:offset + 4]
            if after[3] > 0:
                self.assertEqual(after, before)
            else:
                self.assertEqual(after[:3], before[:3])

    def test_default_v2_radius_and_reviewed_removal_ceiling_remain_unchanged(self):
        source = make_atlas()
        with self.assertRaisesRegex(ValueError, "above the reviewed limit 10"):
            alpha_cleaner.clean_atlas(source, dilation_radius=alpha_cleaner.DILATION_RADIUS)
        with self.assertRaisesRegex(ValueError, "between 0 and 4"):
            alpha_cleaner.clean_atlas(source, dilation_radius=5)

    def test_pinned_bundle_is_non_overwriting_and_exports_four_native_frames(self):
        atlas = make_atlas()
        with tempfile.TemporaryDirectory(prefix="nh-cabir-walk-v3-") as temp:
            temp_root = Path(temp)
            source = temp_root / "atlas.png"
            prompt = temp_root / "atlas.prompt.txt"
            output = temp_root / "cleaned"
            atlas.save(source, format="PNG")
            prompt.write_text("pinned synthetic prompt\n", encoding="utf-8")
            source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
            prompt_sha = hashlib.sha256(prompt.read_bytes()).hexdigest()

            with (
                patch.object(walk_cleaner, "SOURCE_ATLAS", source),
                patch.object(walk_cleaner, "SOURCE_PROMPT", prompt),
                patch.object(walk_cleaner, "OUTPUT_DIRECTORY", output),
                patch.object(walk_cleaner, "PINNED_SOURCE_SHA256", source_sha),
                patch.object(walk_cleaner, "PINNED_PROMPT_SHA256", prompt_sha),
            ):
                original_source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
                result = walk_cleaner.export_review_bundle(source, output)
                self.assertEqual(result, output.resolve())
                self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), original_source_hash)

                receipt = json.loads((output / walk_cleaner.RECEIPT_NAME).read_text(encoding="utf-8"))
                self.assertEqual(receipt["sourceSha256"], source_sha)
                self.assertEqual(receipt["sourcePromptSha256"], prompt_sha)
                self.assertEqual(receipt["cleanupRevision"], "cabir-walk-v3-specific-radius-3")
                native_metadata = json.loads(
                    (output / "native-export/export.json").read_text(encoding="utf-8")
                )
                self.assertEqual(native_metadata["rows"], 2)
                self.assertEqual(native_metadata["columns"], 2)
                self.assertEqual(native_metadata["feetBaselineY"], 268)
                self.assertEqual(len(receipt["nativeFiles"]), 7)
                self.assertEqual(sorted(path.name for path in (output / "native-export").glob("frame-*.png")), [
                    "frame-00.png", "frame-01.png", "frame-02.png", "frame-03.png",
                ])
                with Image.open(output / "native-export/animation-proof.gif") as gif:
                    self.assertEqual(gif.n_frames, 4)
                for frame_path in sorted((output / "native-export").glob("frame-*.png")):
                    with Image.open(frame_path) as frame:
                        self.assertEqual(frame.size, (450, 400))

                output_hashes = {
                    item.relative_to(output): hashlib.sha256(item.read_bytes()).hexdigest()
                    for item in output.rglob("*") if item.is_file()
                }
                with self.assertRaises(FileExistsError):
                    walk_cleaner.export_review_bundle(source, output)
                self.assertEqual(output_hashes, {
                    item.relative_to(output): hashlib.sha256(item.read_bytes()).hexdigest()
                    for item in output.rglob("*") if item.is_file()
                })

    def test_wrong_pinned_hash_fails_before_creating_outputs(self):
        atlas = make_atlas()
        with tempfile.TemporaryDirectory(prefix="nh-cabir-walk-v3-pin-") as temp:
            temp_root = Path(temp)
            source = temp_root / "atlas.png"
            prompt = temp_root / "atlas.prompt.txt"
            output = temp_root / "cleaned"
            atlas.save(source, format="PNG")
            prompt.write_text("pinned synthetic prompt\n", encoding="utf-8")
            with (
                patch.object(walk_cleaner, "SOURCE_ATLAS", source),
                patch.object(walk_cleaner, "SOURCE_PROMPT", prompt),
                patch.object(walk_cleaner, "OUTPUT_DIRECTORY", output),
                patch.object(walk_cleaner, "PINNED_SOURCE_SHA256", "not-the-source-hash"),
                patch.object(walk_cleaner, "PINNED_PROMPT_SHA256", hashlib.sha256(prompt.read_bytes()).hexdigest()),
            ):
                with self.assertRaisesRegex(ValueError, "atlas bytes changed"):
                    walk_cleaner.export_review_bundle(source, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
