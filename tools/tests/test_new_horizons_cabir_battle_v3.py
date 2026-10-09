#!/usr/bin/env python3
"""Focused offline integrity checks for Cabir v3 battle art exports."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image
from io import BytesIO
from tools.tests.nhart_test_resources import ArtPath, resource_names
from tools import import_new_horizons_academy_assets as academy_importer


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import export_new_horizons_cabir_battle_v3 as exporter  # noqa: E402


BASE = ROOT / "assets/new-horizons/creatures/cabir/v3/battle-export-v1"
MASTER = ROOT / "assets/new-horizons/creatures/cabir-master/v3/battle-export-v1"


def _family(root: Path):
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    descriptor = json.loads((root / manifest["descriptor"]).read_text(encoding="utf-8"))
    by_group = {sequence["group"]: sequence["frames"] for sequence in descriptor["sequences"]}
    return manifest, descriptor, by_group


class CabirBattleV3ExportTest(unittest.TestCase):
    def test_descriptors_are_complete_engine_json_with_existing_frame_bytes(self):
        expected = {
            "cabir": {0, 2, 3, 4, 5, 22, 11, 12, 13, 14, 15, 16},
            "cabir-master": {0, 2, 3, 4, 5, 22, 11, 12, 13, 14, 15, 16, 18, 30, 31, 32},
        }
        for family, root in (("cabir", BASE), ("cabir-master", MASTER)):
            with self.subTest(family=family):
                manifest, descriptor, sequences = _family(root)
                self.assertEqual(set(descriptor), {"basepath", "sequences"})
                self.assertEqual(set(sequences), expected[family])
                self.assertTrue(descriptor["basepath"].endswith("/"))
                self.assertEqual(manifest["family"], family)
                self.assertIn("offline provisional", manifest["status"])
                self.assertEqual(manifest["attackClimaxFrameZeroBased"], 2)
                self.assertTrue(root.is_relative_to(ROOT / "assets/new-horizons/creatures"))
                self.assertFalse(root.is_relative_to(ROOT / "Mods"))

                listed = manifest["files"]
                for relative, digest in listed.items():
                    self.assertRegex(digest, r"^[0-9a-f]{64}$")
                    if (root / relative).suffix not in {".png", ".gif"}:
                        self.assertEqual(hashlib.sha256((root / relative).read_bytes()).hexdigest(), digest)

                for group, frame_paths in sequences.items():
                    self.assertTrue(frame_paths)
                    for relative in frame_paths:
                        self.assertTrue(relative.startswith("frames/"))
                        # Superseded authoring frames are private, not selected
                        # runtime assets. Preserve every logical frame/hash record.
                        key = manifest["descriptorBasepath"] + relative
                        self.assertIn(key, listed)
                        self.assertRegex(listed[key], r"^[0-9a-f]{64}$")

    def test_directional_melee_sequences_put_release_on_climax_frame(self):
        for root in (BASE, MASTER):
            with self.subTest(root=root.name):
                manifest, _descriptor, sequences = _family(root)
                self.assertEqual(manifest["attackClimaxFrameZeroBased"], 2)
                front = sequences[12]
                up = sequences[11]
                down = sequences[13]
                for sequence in (front, up, down):
                    self.assertEqual(len(sequence), 4)
                self.assertEqual(up, [front[0], up[1], up[2], front[3]])
                self.assertEqual(down, [front[0], down[1], down[2], front[3]])
                self.assertNotEqual(up[1], up[2])
                self.assertNotEqual(down[1], down[2])

    def test_both_forms_have_distinct_shot_groups_with_shared_directional_endpoints(self):
        for root in (BASE, MASTER):
            with self.subTest(root=root.name):
                manifest, _descriptor, sequences = _family(root)
                self.assertEqual(manifest["attackClimaxFrameZeroBased"], 2)
                front = sequences[15]
                up = sequences[14]
                down = sequences[16]
                for sequence in (front, up, down):
                    self.assertEqual(len(sequence), 4)
                self.assertEqual(up, [front[0], up[1], up[2], front[3]])
                self.assertEqual(down, [front[0], down[1], down[2], front[3]])
                self.assertNotEqual(up[1], up[2])
                self.assertNotEqual(down[1], down[2])

    def test_base_shooting_sources_and_faint_exterior_alpha_are_pinned(self):
        manifest, _descriptor, _sequences = _family(BASE)
        expected = {
            "shoot_front": {
                "sourcePath": "assets/new-horizons/creatures/cabir/v3/shoot-front-v1/candidate-01.png",
                "sourceSha256": "d806036bf1bf0b594cda35c8e24d3d33022a58a3f08213e26e20eedb32edcc38",
                "promptSha256": "3056f56f378a6dc99ab6f347fd9f47861bcbd1b1ff1b063170e9447b510e349e",
                "sourceSize": [1323, 1189],
                "removedPixelCount": 10284,
                "maximumRemovedAlpha": 5,
            },
            "shoot_directions": {
                "sourcePath": "assets/new-horizons/creatures/cabir/v3/shoot-directions-v1/candidate-01.png",
                "sourceSha256": "fe9b4effd297b7b1483f332939199d4ca7d29d3dece11cb6aa94e8bf3e7c3f95",
                "promptSha256": "bced008a74ee7dbddde58209652d822118ab7c0cfed82d64d1a39bca49e18a42",
                "sourceSize": [1222, 1287],
                "removedPixelCount": 8833,
                "maximumRemovedAlpha": 3,
            },
        }
        for key, pins in expected.items():
            with self.subTest(atlas=key):
                source = manifest["sourceSheets"][key]
                for field, value in pins.items():
                    self.assertEqual(source[field], value)
                self.assertFalse(source["sourceFileModified"])
                self.assertEqual(source["authorizedAlphaOnlyCleanup"], [])
                self.assertEqual(source["auxiliaryComponentsAssigned"], [])

    def test_master_repair_is_an_explicit_front_gesture_alias(self):
        manifest, _descriptor, sequences = _family(MASTER)
        self.assertEqual(manifest["intentionalAliases"], {"18": [31], "30": [31], "32": [31]})
        for group in (18, 30, 31, 32):
            self.assertEqual(sequences[group], sequences[31])
        self.assertEqual(len(sequences[31]), 4)

    def test_authorized_master_alpha_cleanup_and_detached_effect_assignments_are_pinned(self):
        manifest, _descriptor, _sequences = _family(MASTER)
        sheets = manifest["sourceSheets"]
        self.assertFalse(sheets["walk"]["sourceFileModified"])
        self.assertEqual(
            [(item["x"], item["y"], item["expectedAlpha"]) for item in sheets["walk"]["authorizedAlphaOnlyCleanup"]],
            [(1078, 87, 19), (437, 1007, 17)],
        )
        self.assertEqual(sheets["shoot_front"]["authorizedAlphaOnlyCleanup"][0]["expectedAlpha"], 14)
        self.assertFalse(sheets["shoot_front"]["sourceFileModified"])
        self.assertEqual(
            [item["assignedPoseIndex"] for item in sheets["shoot_front"]["auxiliaryComponentsAssigned"]],
            [1, 3],
        )
        self.assertEqual(
            [item["assignedPoseIndex"] for item in sheets["repair"]["auxiliaryComponentsAssigned"]],
            [2, 2],
        )
        self.assertEqual(
            [item["sourceGlobalBBox"] for item in sheets["repair"]["auxiliaryComponentsAssigned"]],
            [[556, 841, 597, 906], [505, 983, 545, 1006]],
        )

    def test_family_contact_sheets_use_one_uniform_nearest_scale(self):
        for root in (BASE, MASTER):
            with self.subTest(root=root.name):
                manifest, _descriptor, _sequences = _family(root)
                index = json.loads((root / "review/family-index.json").read_text(encoding="utf-8"))
                self.assertEqual(index["uniformContactNearestScale"], 2)
                self.assertEqual(index["status"], "offline provisional preview; not installed or accepted")
                # Review sheets are private. Their retained index specifies the
                # same exact nearest-neighbour layout; synthesize one real frame.
                frame = Image.new("RGBA", (450, 400))
                frame.paste((73, 19, 41, 255), (180, 220, 210, 280))
                contact_bytes = exporter._contact_master([("synthetic", frame)], factor=2)
                with Image.open(BytesIO(contact_bytes)) as contact:
                    self.assertEqual(contact.mode, "RGBA")
                    self.assertEqual(contact.size, (4 * 360, 200))
                self.assertGreater(len(index["frames"]), 0)
                self.assertEqual(index["family"], manifest["family"])

    def test_installed_module_descriptors_and_images_match_pinned_exports(self):
        # Current registration selects the complete handoff, not the superseded
        # v3 draft descriptors. Check actual package descriptors and every frame.
        config = academy_importer.load_jsonc(ROOT / "Mods/new-horizons/Content/config/creatures/tower.json")
        for creature in ("core:gremlin", "core:masterGremlin"):
            descriptor_name = config[creature]["graphics"]["animation"].removesuffix(".def") + ".json"
            descriptor = json.loads((ArtPath() / descriptor_name).read_text())
            count = 0
            for sequence in descriptor["sequences"]:
                for relative in sequence["frames"]:
                    path = ArtPath() / descriptor.get("basepath", "") / relative
                    self.assertTrue(path.is_file(), str(path))
                    with path.open_image() as frame:
                        self.assertEqual(frame.format, "PNG")
                        self.assertIsNotNone(frame.convert("RGBA").getchannel("A").getbbox())
                    count += 1
            self.assertGreater(count, 0)

    def test_refresh_requires_prior_manifest_and_exact_old_file_hashes(self):
        with tempfile.TemporaryDirectory(prefix="nh-cabir-refresh-test-") as temporary:
            root = Path(temporary)
            content = b"reviewed old bytes\n"
            (root / "frame.png").write_bytes(content)
            (root / "manifest.json").write_text(json.dumps({
                "family": "cabir",
                "status": "offline provisional test fixture",
                "files": {"frame.png": hashlib.sha256(content).hexdigest()},
            }), encoding="utf-8")
            exporter._validate_previous_export(root, "cabir")
            (root / "frame.png").write_bytes(b"unreviewed replacement\n")
            with self.assertRaisesRegex(ValueError, "changed since its manifest"):
                exporter._validate_previous_export(root, "cabir")
            archive_root = root / "archives"
            (archive_root / "cabir-previous").mkdir(parents=True)
            (archive_root / "cabir-previous-2").mkdir()
            self.assertEqual(
                exporter._next_refresh_backup(archive_root, "cabir"),
                archive_root / "cabir-previous-3",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
