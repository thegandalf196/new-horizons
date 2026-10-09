"""Focused tests for the reusable Cabir native pose preview helper."""

import json
import hashlib
from dataclasses import replace
from pathlib import Path
import sys
import unittest

from PIL import Image, ImageDraw
from tools.tests.nhart_test_resources import ArtPath


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_cabir_pose_preview as preview
import extract_new_horizons_cabir_poses as extractor


def make_pose(name: str, box: tuple[int, int, int, int]) -> preview.NativePose:
    image = Image.new("RGBA", (100, 100), (0, 0, 0, 0))
    ImageDraw.Draw(image).rectangle(box, fill=(170, 90, 40, 255))
    return preview.NativePose(
        name=name,
        image=image,
        source_path="fixture.png",
        source_sha256="fixture-sha",
        source_origin=(0, 0),
        extraction={"kind": "synthetic test fixture"},
    )


class CabirPosePreviewTest(unittest.TestCase):
    def test_shared_scale_native_canvas_and_ground_pivots(self):
        poses = [
            make_pose("reference", (20, 10, 40, 69)),
            make_pose("shorter", (28, 30, 42, 69)),
        ]
        artifacts, metadata, frames = preview.build_preview_group("fixture", poses)

        self.assertEqual(len(frames), 2)
        self.assertTrue(all(frame.mode == "RGBA" and frame.size == (450, 400) for frame in frames))
        self.assertEqual(metadata["targetRootPivot"], [196.5, 268.0])
        self.assertEqual(metadata["scaleCalibration"]["targetBodyHeight"], 60.0)
        scale = metadata["scaleCalibration"]["uniformScale"]
        self.assertEqual(scale, 1.0)
        self.assertEqual([pose["uniformSheetScale"] for pose in metadata["poses"]], [scale, scale])
        self.assertEqual(metadata["poses"][0]["resizedSourceSize"], [100, 100])
        self.assertEqual(metadata["poses"][1]["resizedSourceSize"], [100, 100])
        self.assertEqual(metadata["poses"][0]["outputPivotActual"], [196.5, 268.0])
        self.assertEqual(metadata["poses"][1]["outputPivotActual"], [196.5, 268.0])
        self.assertIn("exports/fixture/contact-sheet-4x.png", artifacts)
        self.assertIn("exports/fixture/animation-proof.gif", artifacts)
        self.assertIn("exports/fixture/alignment.json", artifacts)

    def test_rejects_empty_group_and_bad_reference_index(self):
        with self.assertRaisesRegex(ValueError, "group is empty"):
            preview.build_preview_group("empty", [])
        with self.assertRaisesRegex(ValueError, "reference pose index"):
            preview.build_preview_group("bad-reference", [make_pose("only", (20, 10, 40, 69))], 1)


class CabirPinnedV3PreviewParityTest(unittest.TestCase):
    def test_extractor_source_roots_are_explicit_and_fail_closed(self):
        for spec in extractor.SOURCES.values():
            with self.subTest(action=spec.action):
                self.assertIsNotNone(spec.allowed_source_root)
                extractor._validate_source_location(spec, spec.source_path)
                outside = replace(spec, allowed_source_root=ROOT / "outside-pinned-action")
                with self.assertRaisesRegex(ValueError, "outside its pinned Cabir action directory"):
                    extractor._validate_source_location(outside, spec.source_path)

    def _check_action(self, directory: str, group_name: str, pose_names: list[str]):
        descriptor = json.loads((ArtPath() / "NH_CabirCompleteHandoff.json").read_text())
        groups = {sequence["group"]: sequence["frames"] for sequence in descriptor["sequences"]}
        selected = (11, 12, 13) if directory == "melee-front-v1" else (3, 5)
        poses = []
        for group in selected:
            self.assertTrue(groups[group])
            for index, name in enumerate(groups[group]):
                resource = ArtPath() / (descriptor["basepath"] + name)
                image = resource.open_image().convert("RGBA")
                self.assertIsNotNone(image.getbbox())
                # Native exports have full battle-canvas padding; the pose
                # aligner takes the occupied source crop, not that padding.
                image = image.crop(image.getbbox())
                poses.append(preview.NativePose(name=f"group-{group}-{index}", image=image,
                    source_path=str(resource), source_sha256=hashlib.sha256(resource.read_bytes()).hexdigest(),
                    source_origin=(0, 0), extraction={"kind": "verified selected runtime frame"}))
        artifacts, metadata, frames = preview.build_preview_group(group_name, poses)
        self.assertEqual(metadata["canvas"], [450, 400])
        self.assertEqual(metadata["scaleCalibration"]["targetBodyHeight"], 60.0)
        self.assertTrue(all(frame.size == (450, 400) for frame in frames))
        self.assertEqual(len(frames), len(poses))
        self.assertEqual(artifacts, preview.build_preview_group(group_name, poses)[0])

    def test_selected_melee_frames_produce_reproducible_native_preview(self):
        self._check_action(
            "melee-front-v1",
            "base-melee-front-v3",
            ["ready", "windup", "thrust", "recovery"],
        )

    def test_selected_reaction_frames_produce_reproducible_native_preview(self):
        self._check_action(
            "reactions-v1",
            "base-reactions-v3",
            ["hit", "brace", "dying", "dead"],
        )


if __name__ == "__main__":
    unittest.main()
