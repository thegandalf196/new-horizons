"""Focused tests for lossless Cabir action-atlas pose separation."""

from pathlib import Path
import sys
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import extract_new_horizons_cabir_poses as extractor


def make_sheet(*, missing_pose: bool = False, merged_pose: bool = False) -> Image.Image:
    image = Image.new("RGBA", (100, 100), (0, 0, 0, 0))
    # Pose zero intentionally crosses the nominal x=50 quadrant boundary.
    boxes = ((46, 10, 54, 20), (78, 10, 86, 20), (10, 75, 18, 85), (75, 75, 83, 85))
    colors = ((201, 31, 41), (31, 202, 51), (41, 51, 203), (211, 181, 31))
    for box, color in zip(boxes[:3] if missing_pose else boxes, colors):
        x0, y0, x1, y1 = box
        for y in range(y0, y1):
            for x in range(x0, x1):
                image.putpixel((x, y), (*color, 255))
    # Semitransparent edge pixels inside the reviewed radius must be unchanged.
    image.putpixel((45, 15), (220, 120, 90, 12))
    # A detached faint source speck is the only kind of visible pixel eligible
    # for removal by this task's explicitly reviewed policy.
    image.putpixel((1, 1), (180, 170, 160, 5))
    if merged_pose:
        for x in range(54, 79):
            image.putpixel((x, 15), (111, 112, 113, 255))
    return image


def test_spec() -> extractor.PoseSource:
    return extractor.PoseSource(
        action="fixture",
        source_path=ROOT / "fixture.png",
        output_dir=ROOT / "fixture-separated",
        source_sha256="fixture-sha",
        seed_alpha_threshold=16,
        major_component_min_area=50,
    )


class CabirPoseSeparationTest(unittest.TestCase):
    def test_cross_quadrant_pose_is_preserved_on_shared_source_canvas(self):
        source = make_sheet()
        original = source.tobytes()
        frames, receipt = extractor.separate_poses(source, test_spec(), "fixture-sha")

        self.assertEqual(len(frames), 4)
        self.assertTrue(all(frame.size == frames[0].size for frame in frames))
        self.assertEqual(source.tobytes(), original, "separation mutated its source image")
        self.assertEqual(receipt["sourceSha256"], "fixture-sha")
        self.assertEqual(receipt["sourceComponentCountAtThreshold"], 4)
        self.assertEqual(receipt["selectedPoseCount"], 4)
        self.assertTrue(receipt["commonSourceCanvas"]["sourceOffsetsPreserved"])
        self.assertFalse(receipt["commonSourceCanvas"]["resized"])
        self.assertEqual(receipt["poses"][0]["sourceGlobalBBox"], [46, 10, 54, 20])
        self.assertEqual(receipt["poses"][0]["row"], 0)
        self.assertEqual(receipt["poses"][0]["column"], 0)

        common_x, common_y, _common_right, _common_bottom = receipt["commonSourceCanvas"]["globalBBox"]
        first_pose = frames[0]
        # x=53 lies in the right half of the nominal 2x2 atlas, but belongs to
        # the first complete silhouette and must not be clipped or reassigned.
        self.assertEqual(first_pose.getpixel((53 - common_x, 15 - common_y)), source.getpixel((53, 15)))
        self.assertEqual(first_pose.getpixel((45 - common_x, 15 - common_y)), source.getpixel((45, 15)))
        self.assertEqual(receipt["removedPixelCount"], 1)
        self.assertEqual(receipt["maximumRemovedAlpha"], 5)
        self.assertEqual(source.getpixel((1, 1)), (180, 170, 160, 5))

        other_pose = frames[1]
        self.assertEqual(other_pose.getpixel((53 - common_x, 15 - common_y))[3], 0)

    def test_missing_or_merged_silhouettes_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "requires exactly 4 major silhouettes"):
            extractor.separate_poses(make_sheet(missing_pose=True), test_spec())
        with self.assertRaisesRegex(ValueError, "requires exactly 4 major silhouettes"):
            extractor.separate_poses(make_sheet(merged_pose=True), test_spec())

    def test_meaningful_external_alpha_is_never_discarded(self):
        source = make_sheet()
        source.putpixel((1, 1), (180, 170, 160, 11))
        with self.assertRaisesRegex(ValueError, "refusing to discard a potentially meaningful pixel"):
            extractor.separate_poses(source, test_spec())


if __name__ == "__main__":
    unittest.main()
