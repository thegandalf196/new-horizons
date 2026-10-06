#!/usr/bin/env python3
"""Focused reproducibility and geometry checks for Cabir portrait exports."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
EXPORTER = ROOT / "tools/export_new_horizons_cabir_portraits_v3.py"
CREATURES = {
    "cabir": {
        "directory": ROOT / "assets/new-horizons/creatures/cabir/v3/portrait-export-v2",
        "source": ROOT / "assets/new-horizons/creatures/cabir/v3/standing-master.png",
        "sourceSha256": "e9e5cfacb4cafa8f55c0160a400a4f0c6ea3426d77f631c928de27eaa4033060",
        "prompt": ROOT / "assets/new-horizons/creatures/cabir/v3/standing.prompt.txt",
        "promptSha256": "66a9aa4bf1be4f6f964ace0f038cde03c134556a756d87a7f6bb106a2be0a082",
        "large": "NH_cabir_icon_large.png",
        "small": "NH_cabir_icon_small.png",
        "legacyDirectory": ROOT / "assets/new-horizons/creatures/cabir/v3/portrait-export-v1",
        "legacyHashes": {
            "NH_cabir_icon_large.png": "09b3de803b4db2e29343346753ca72aefd3fee35f799429f340631d0994337b1",
            "NH_cabir_icon_small.png": "1fb6843cfb3ced0fb9c468d7b101817c87ac4ccd834500f09d6bf242071c1400",
        },
    },
    "cabirMaster": {
        "directory": ROOT / "assets/new-horizons/creatures/cabir-master/v3/portrait-export-v2",
        "source": ROOT / "assets/new-horizons/creatures/cabir-master/v3/standing-master.png",
        "sourceSha256": "c1c88872bf0cc8e969f9d883b289aff86071db31241d35a16255b74532fbca15",
        "prompt": ROOT / "assets/new-horizons/creatures/cabir-master/v3/standing.prompt.txt",
        "promptSha256": "3b97121db995db93b8ec57e775b147c3edfca9da5d0a3696dd471ff67390a5b7",
        "large": "NH_cabirMaster_icon_large.png",
        "small": "NH_cabirMaster_icon_small.png",
        "legacyDirectory": ROOT / "assets/new-horizons/creatures/cabir-master/v3/portrait-export-v1",
        "legacyHashes": {
            "NH_cabirMaster_icon_large.png": "a1cd9a491c52354aaa3182586542ae28661b8647a9c2b0e5182426a1fe481d93",
            "NH_cabirMaster_icon_small.png": "fb280dc34405bb184baadfc6078ab8ffaf41042499bd6d529e4f62870e815ab1",
        },
    },
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class NewHorizonsCabirPortraitV3Test(unittest.TestCase):
    def test_pinned_exporter_reproduces_all_files(self):
        subprocess.run([sys.executable, str(EXPORTER), "--check"], cwd=ROOT, check=True)

    def test_previous_backdrop_exports_remain_unchanged(self):
        for spec in CREATURES.values():
            for name, expected_hash in spec["legacyHashes"].items():
                with self.subTest(legacyImage=name):
                    self.assertEqual(sha256(spec["legacyDirectory"] / name), expected_hash)

    def test_source_master_and_prompt_hashes_are_pinned(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                self.assertEqual(sha256(spec["source"]), spec["sourceSha256"])
                self.assertEqual(sha256(spec["prompt"]), spec["promptSha256"])
                receipt = json.loads((spec["directory"] / "receipt.json").read_text(encoding="utf-8"))
                self.assertEqual(receipt["source"]["sha256"], spec["sourceSha256"])
                self.assertEqual(receipt["source"]["promptSha256"], spec["promptSha256"])
                self.assertFalse(receipt["alphaCleanup"]["sourceMasterModified"])

    def test_native_geometry_and_review_preview_scales(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                paths = (
                    (spec["directory"] / spec["large"], (58, 64)),
                    (spec["directory"] / spec["small"], (32, 32)),
                )
                for image_path, size in paths:
                    with Image.open(image_path) as image:
                        self.assertEqual(image.mode, "RGBA")
                        self.assertEqual(image.size, size)
                for name, size in ((spec["large"], (232, 256)), (spec["small"], (128, 128))):
                    preview_path = spec["directory"] / "previews" / name.replace(".png", "_4x.png")
                    with Image.open(preview_path) as preview:
                        self.assertEqual(preview.mode, "RGBA")
                        self.assertEqual(preview.size, size)

    def test_alpha_cleanup_is_alpha_only_and_bounded(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                receipt = json.loads((spec["directory"] / "receipt.json").read_text(encoding="utf-8"))
                cleanup = receipt["alphaCleanup"]
                self.assertEqual(cleanup["seedThreshold"], 16)
                self.assertEqual(cleanup["dilationRadius"], 4)
                self.assertLessEqual(cleanup["maximumRemovedAlpha"], 10)
                self.assertIn("only alpha is set to zero", cleanup["removedPixels"])
                self.assertIn("original source RGBA bytes are preserved", cleanup["retainedRgba"])
                self.assertEqual(sum(cleanup["removedAlphaHistogram"].values()), cleanup["removedPixelCount"])

    def test_small_icon_is_avatar_crop_with_transparent_background(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                receipt = json.loads((spec["directory"] / "receipt.json").read_text(encoding="utf-8"))
                small = receipt["composition"]["small"]
                self.assertEqual(small["outputSize"], [32, 32])
                self.assertIn("intentional right-facing head/upper-body avatar crop", small["intent"])
                self.assertEqual(small["background"], "transparent RGBA; no background is composited")
                self.assertEqual(receipt["backgroundTreatment"], "transparent RGBA; the prior baked Academy scenery is not composited")
                self.assertEqual(receipt["purchaserPixels"], "none read or copied")
                self.assertEqual(receipt["status"].startswith("provisional"), True)

    def test_all_four_runtime_pngs_are_transparent_copies_of_versioned_exports(self):
        for spec in CREATURES.values():
            for name in (spec["large"], spec["small"]):
                with self.subTest(image=name):
                    exported = spec["directory"] / name
                    runtime = ROOT / "Mods/new-horizons/Images" / name
                    self.assertEqual(exported.read_bytes(), runtime.read_bytes())
                    with Image.open(runtime) as image:
                        self.assertEqual(image.mode, "RGBA")
                        self.assertEqual(image.getchannel("A").getpixel((0, 0)), 0)
                        self.assertIsNotNone(image.getchannel("A").getbbox())

    def test_large_portrait_contains_full_body_without_crop(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                receipt = json.loads((spec["directory"] / "receipt.json").read_text(encoding="utf-8"))
                large = receipt["composition"]["large"]
                self.assertIn("entire cleaned silhouette is contained, not cropped", large["intent"])
                self.assertEqual(large["sourceCrop"], receipt["alphaCleanup"]["cleanedAlphaBounds"])
                with Image.open(spec["directory"] / spec["large"]) as image:
                    self.assertEqual(image.getchannel("A").getpixel((0, 0)), 0)


if __name__ == "__main__":
    unittest.main()
