#!/usr/bin/env python3
"""Focused reproducibility and geometry checks for Cabir portrait exports."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest

from PIL import Image, ImageDraw
from tools.tests.nhart_test_resources import ArtPath
from tools import export_new_horizons_cabir_portraits_v3 as exporter
from tools import import_new_horizons_academy_assets as academy_importer


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


def selected_name(spec, size):
    family = "cabir-master" if spec is CREATURES["cabirMaster"] else "cabir"
    stem = "cabir_master" if family == "cabir-master" else "cabir"
    return f"cabir-handoff/{family}/icons/NH_{stem}_handoff_icon_{size}.png"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class NewHorizonsCabirPortraitV3Test(unittest.TestCase):
    def test_exporter_requires_explicit_private_workspace(self):
        for arguments in (["--check"], ["--check", "--private-root", str(ROOT)]):
            result = subprocess.run([sys.executable, str(EXPORTER), *arguments],
                                    cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("private", result.stderr)

    def test_cleanup_and_portrait_geometry_with_synthetic_private_input(self):
        master = Image.new("RGBA", (1323, 1189), (7, 8, 9, 0))
        ImageDraw.Draw(master).rectangle((200, 60, 1180, 1150), fill=(90, 120, 150, 255))
        master.putpixel((0, 0), (7, 8, 9, 8))
        cleaned, receipt = exporter._clean_master(master)
        self.assertEqual(cleaned.getpixel((0, 0)), (7, 8, 9, 0))
        self.assertEqual(master.getpixel((0, 0)), (7, 8, 9, 8))
        self.assertEqual(receipt["maximumRemovedAlpha"], 8)
        large, small, _composition = exporter._render_portraits(cleaned, cleaned.getchannel("A").getbbox())
        self.assertEqual(large.size, (58, 64))
        self.assertEqual(small.size, (32, 32))
        self.assertEqual(large.getpixel((0, 0))[3], 0)

    def test_source_master_and_prompt_hashes_are_pinned(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                self.assertEqual(exporter.CREATURES[creature_id]["sourceSha256"], spec["sourceSha256"])
                self.assertEqual(sha256(spec["prompt"]), spec["promptSha256"])
                receipt = json.loads((spec["directory"] / "receipt.json").read_text(encoding="utf-8"))
                self.assertEqual(receipt["source"]["sha256"], spec["sourceSha256"])
                self.assertEqual(receipt["source"]["promptSha256"], spec["promptSha256"])
                self.assertFalse(receipt["alphaCleanup"]["sourceMasterModified"])

    def test_native_geometry_and_review_preview_scales(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                paths = (
                    (ArtPath() / selected_name(spec, "large"), (58, 64)),
                    (ArtPath() / selected_name(spec, "small"), (32, 32)),
                )
                for image_path, size in paths:
                    with image_path.open_image() as image:
                        self.assertEqual(image.mode, "RGBA")
                        self.assertEqual(image.size, size)
                for name, size in ((spec["large"], (232, 256)), (spec["small"], (128, 128))):
                    with (ArtPath() / selected_name(spec, "large" if name == spec["large"] else "small")).open_image() as native:
                        preview = native.resize(size, Image.Resampling.NEAREST)
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

    def test_all_four_selected_runtime_pngs_resolve_with_current_registration(self):
        for spec in CREATURES.values():
            for name in (spec["large"], spec["small"]):
                with self.subTest(image=name):
                    runtime = ArtPath() / selected_name(spec, "large" if name == spec["large"] else "small")
                    self.assertTrue(runtime.is_file())
                    config = academy_importer.load_jsonc(ROOT / "Mods/new-horizons/Content/config/creatures/tower.json")
                    creature = "core:masterGremlin" if spec is CREATURES["cabirMaster"] else "core:gremlin"
                    field = "iconLarge" if name == spec["large"] else "iconSmall"
                    self.assertEqual(config[creature]["graphics"][field], runtime.resource.removeprefix("SPRITES/"))
                    with runtime.open_image() as image:
                        self.assertEqual(image.mode, "RGBA")
                        self.assertIsNotNone(image.getchannel("A").getbbox())

    def test_superseded_large_portrait_full_body_intent_is_retained(self):
        for creature_id, spec in CREATURES.items():
            with self.subTest(creature=creature_id):
                receipt = json.loads((spec["directory"] / "receipt.json").read_text(encoding="utf-8"))
                large = receipt["composition"]["large"]
                self.assertIn("entire cleaned silhouette is contained, not cropped", large["intent"])
                self.assertEqual(large["sourceCrop"], receipt["alphaCleanup"]["cleanedAlphaBounds"])


if __name__ == "__main__":
    unittest.main()
