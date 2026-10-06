#!/usr/bin/env python3
"""Focused Academy art registration and provenance checks."""

import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "Mods/new-horizons/Images"
ACADEMY_SOURCE = ROOT / "assets/new-horizons/academy"
ART_PATCH = ROOT / "Mods/new-horizons/Content/config/factions/academyArt.json"
OPTIONS_TAB = ROOT / "client/lobby/OptionsTab.cpp"
sys.path.insert(0, str(ROOT / "tools"))
import import_new_horizons_academy_assets as academy_importer


def read_json(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


class NewHorizonsAcademyArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = read_json("Mods/new-horizons/Content/config/factions/academyArt.json")["core:tower"]
        cls.town = cls.patch["town"]
        cls.structures = cls.town["structures"]

    def test_faction_identity_terrain_and_visual_scope(self):
        self.assertEqual(self.patch["name"], "Academy")
        self.assertEqual(self.patch["nativeTerrain#override"], ["sand"])
        self.assertNotIn("buildings", self.town)
        self.assertNotIn("hallSlots", self.town)
        self.assertNotIn("creatures", self.town)
        self.assertEqual(self.town["buildingsIcons"], "NH_tower_buildings")

        expected = {
            "dwellingLvl4": ("NH_ACADEMY_TBTWDW_4", 511, 75),
            "dwellingLvl5": ("NH_ACADEMY_TBTWDW_3", 613, 95),
            "dwellingUpLvl4": ("NH_ACADEMY_TBTWUP_4", 511, 8),
            "dwellingUpLvl5": ("NH_ACADEMY_TBTWUP_3", 613, 74),
        }
        for name, (resource, x, y) in expected.items():
            with self.subTest(structure=name):
                structure = self.structures[name]
                self.assertEqual((structure["animation"], structure["x"], structure["y"]), (resource, x, y))

        roof = self.structures["academyRoof"]
        self.assertEqual((roof["animation"], roof["x"], roof["y"], roof["z"]),
                         ("NH_ACADEMY_TOWN_ROOF", 665, 255, 4))
        for interaction_field in ("area", "border", "campaignBonus", "builds"):
            self.assertNotIn(interaction_field, roof)

    def test_registered_art_masks_icons_and_map_bodies_resolve(self):
        for name, structure in self.structures.items():
            if name == "academyRoof":
                resource = structure["animation"]
                descriptor = read_json(f"Mods/new-horizons/Images/{resource}.json")
                image_path = descriptor["images"][0]["file"]
            else:
                for field in ("area", "border", "campaignBonus"):
                    with self.subTest(structure=name, field=field):
                        self.assertTrue((IMAGES / structure[field]).is_file())
                descriptor = read_json(f"Mods/new-horizons/Images/{structure['animation']}.json")
                image_path = descriptor["images"][0]["file"]
                with Image.open(IMAGES / structure["area"]) as area, Image.open(IMAGES / structure["border"]) as border:
                    with Image.open(IMAGES / image_path) as art:
                        self.assertEqual(area.size, art.size, name)
                        self.assertEqual(border.size, art.size, name)
                    self.assertGreater(area.getchannel("A").getbbox()[2], 0, name)
                    self.assertGreater(border.getchannel("A").getbbox()[2], 0, name)
            self.assertTrue((IMAGES / image_path).is_file(), name)

        hall = read_json("Mods/new-horizons/Images/NH_tower_buildings.json")["images"]
        self.assertEqual(len(hall), 44)
        self.assertEqual([entry["frame"] for entry in hall], list(range(44)))
        for frame, entry in enumerate(hall):
            source_frame = {33: 34, 34: 33, 40: 41, 41: 40}.get(frame, frame)
            self.assertEqual(entry["file"], f"NH_academy/ui/hall/frame-{source_frame:03d}.png")
            self.assertTrue((IMAGES / entry["file"]).is_file())

        for resource, filename in (
            ("NH_ACADEMY_VILLAGE_BODY", "NH_academy_village_body.png"),
            ("NH_ACADEMY_FORT_BODY", "NH_academy_fort_body.png"),
            ("NH_ACADEMY_CAPITOL_BODY", "NH_academy_capitol_body.png"),
        ):
            descriptor = read_json(f"Mods/new-horizons/Images/{resource}.json")
            with Image.open(IMAGES / descriptor["images"][0]["file"]) as body:
                self.assertEqual(body.size, (192, 192))
            self.assertTrue((IMAGES / filename).is_file())

        for faction_icons in self.town["icons"].values():
            for size in ("large", "small"):
                normal = IMAGES / faction_icons["normal"][size]
                built_fallback = IMAGES / faction_icons["built"][size]
                self.assertTrue(normal.is_file())
                self.assertTrue(faction_icons["built"][size].endswith("_built.png"))
                self.assertTrue(built_fallback.is_file())
                self.assertEqual(built_fallback.read_bytes(), normal.read_bytes())

    def test_astronomy_attachment_survives_generated_patch(self):
        core = academy_importer.load_jsonc(ROOT / "config/factions/tower.json")
        ranks = read_json("Mods/new-horizons/Content/config/factions/towerCreatureRanks.json")
        generated = academy_importer.academy_patch(ROOT, core, ranks)
        actual = self.structures["special2"]
        expected = generated["core:tower"]["town"]["structures"]["special2"]
        expected.pop("_generatedImagePath")
        self.assertEqual(actual, expected)
        self.assertEqual(actual["x"], academy_importer.ASTRONOMY_TOWER_X)
        self.assertNotIn("y", actual)
        self.assertNotIn("z", actual)
        self.assertEqual(actual["animation"], "NH_ACADEMY_TBTWEXT0")
        self.assertEqual(actual["area"], "NH_academy/town/masks/special2-area.png")
        self.assertEqual(actual["border"], "NH_academy/town/masks/special2-border.png")

    def test_reviewed_v2_icon_revision_is_pinned_and_installed_exactly(self):
        revision = academy_importer.load_icon_revision(
            ROOT,
            academy_importer.APPROVED_ICON_REVISION_MANIFEST_SHA256,
        )
        self.assertEqual(revision["manifest"]["revision"], "v2")
        self.assertEqual(
            revision["manifest"]["builtFallbackPolicy"],
            "byte-identical-to-active-normal",
        )
        for slot, expected in academy_importer.ICON_REVISION_SLOTS.items():
            with self.subTest(slot=slot):
                runtime = expected["runtime"]
                export = revision["exports_by_runtime"][runtime]
                active = IMAGES / runtime
                built = IMAGES / runtime.replace("_normal.png", "_built.png")
                self.assertEqual(active.read_bytes(), export)
                self.assertEqual(built.read_bytes(), export)

    def test_icon_revision_manifest_and_exports_reject_unreviewed_changes(self):
        pin = academy_importer.APPROVED_ICON_REVISION_MANIFEST_SHA256
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            source_revision = ROOT / academy_importer.ICON_REVISION_ROOT
            target_revision = temp_root / academy_importer.ICON_REVISION_ROOT
            shutil.copytree(source_revision, target_revision)

            manifest_path = temp_root / academy_importer.ICON_REVISION_MANIFEST
            manifest_bytes = manifest_path.read_bytes()
            manifest_path.write_bytes(manifest_bytes + b" ")
            with self.assertRaisesRegex(RuntimeError, "manifest changed"):
                academy_importer.load_icon_revision(temp_root, pin)
            manifest_path.write_bytes(manifest_bytes)

            export_path = target_revision / "exports/NH_academy_village_large_normal.png"
            export_bytes = export_path.read_bytes()
            export_path.write_bytes(export_bytes + b"\0")
            with self.assertRaisesRegex(ValueError, "export bytes do not match"):
                academy_importer.load_icon_revision(temp_root, pin)
            export_path.write_bytes(export_bytes)

            altered_manifest = json.loads(manifest_bytes)
            altered_manifest["icons"]["fort.large.normal"]["runtime"] = "unapproved.png"
            altered_bytes = json.dumps(altered_manifest, indent="\t", ensure_ascii=False).encode("utf-8") + b"\n"
            manifest_path.write_bytes(altered_bytes)
            altered_pin = hashlib.sha256(altered_bytes).hexdigest()
            with self.assertRaisesRegex(ValueError, "changed the approved fort.large.normal runtime mapping"):
                academy_importer.load_icon_revision(temp_root, altered_pin)

        legacy = b"package normal pixels"
        reviewed = b"reviewed v2 export pixels"
        unknown = b"unreviewed local edits"

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            normal = root / academy_importer.IMAGE_ROOT / "NH_academy_fort_small_normal.png"
            built = root / academy_importer.IMAGE_ROOT / "NH_academy_fort_small_built.png"
            normal.parent.mkdir(parents=True)
            normal.write_bytes(legacy)
            built.write_bytes(legacy)
            academy_importer.install_curated_icon(
                root, normal.name, reviewed, legacy, check_only=False
            )
            self.assertEqual(normal.read_bytes(), reviewed)
            self.assertEqual(built.read_bytes(), reviewed)

            built.write_bytes(unknown)
            with self.assertRaisesRegex(RuntimeError, "unrecognized Academy built-icon fallback pixels"):
                academy_importer.install_curated_icon(
                    root, normal.name, b"next reviewed export", reviewed, check_only=False
                )
            self.assertEqual(normal.read_bytes(), reviewed)
            self.assertEqual(built.read_bytes(), unknown)

    def test_reviewed_v3_map_revision_is_pinned_installed_and_native_framed(self):
        revision = academy_importer.load_map_revision(
            ROOT,
            academy_importer.APPROVED_MAP_REVISION_V3_MANIFEST_SHA256,
            revision="v3",
        )
        self.assertEqual(revision["manifest"]["revision"], "v3")
        for name, expected in academy_importer.MAP_REVISION_V3_SLOTS.items():
            with self.subTest(body=name):
                runtime = IMAGES / expected["runtime"]
                reviewed = revision["exports_by_runtime"][expected["runtime"]]
                self.assertEqual(runtime.read_bytes(), reviewed)
                with Image.open(runtime) as image:
                    self.assertEqual(list(image.size), [192, 192])
                    alpha_bounds = image.convert("RGBA").getchannel("A").getbbox()
                source_box = expected["sourceSolidBox"]
                self.assertIsNotNone(alpha_bounds)
                self.assertGreaterEqual(alpha_bounds[0], source_box["left"])
                self.assertGreaterEqual(alpha_bounds[1], source_box["top"])
                self.assertLessEqual(alpha_bounds[2], source_box["left"] + source_box["width"])
                self.assertLessEqual(alpha_bounds[3], source_box["top"] + source_box["height"])

    def test_map_revision_pin_and_three_body_install_fail_closed(self):
        pin = academy_importer.APPROVED_MAP_REVISION_MANIFEST_SHA256
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            source_revision = ROOT / academy_importer.MAP_REVISION_ROOT
            target_revision = temp_root / academy_importer.MAP_REVISION_ROOT
            shutil.copytree(source_revision, target_revision)
            for relative in ("integration/academy-assets.json",):
                source = ROOT / academy_importer.SOURCE_ROOT / relative
                destination = temp_root / academy_importer.SOURCE_ROOT / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
            for expected in academy_importer.MAP_REVISION_SLOTS.values():
                source = ROOT / academy_importer.SOURCE_ROOT / expected["sourceMaster"]
                destination = temp_root / academy_importer.SOURCE_ROOT / expected["sourceMaster"]
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)

            manifest_path = temp_root / academy_importer.MAP_REVISION_MANIFEST
            manifest_bytes = manifest_path.read_bytes()
            loaded = academy_importer.load_map_revision(temp_root, pin)
            self.assertEqual(loaded["manifest_sha256"], pin)

            manifest_path.write_bytes(manifest_bytes + b" ")
            with self.assertRaisesRegex(RuntimeError, "manifest changed"):
                academy_importer.load_map_revision(temp_root, pin)
            manifest_path.write_bytes(manifest_bytes)

            altered_manifest = json.loads(manifest_bytes)
            export_path = target_revision / "exports/NH_academy_village_body.png"
            export_bytes = export_path.read_bytes()
            export_path.write_bytes(export_bytes + b"\0")
            with self.assertRaisesRegex(ValueError, "export bytes do not match"):
                academy_importer.load_map_revision(temp_root, pin)
            export_path.write_bytes(export_bytes)

            altered_manifest["bodies"]["village"]["runtime"] = "unapproved.png"
            altered_bytes = json.dumps(altered_manifest, indent="\t", ensure_ascii=False).encode("utf-8") + b"\n"
            manifest_path.write_bytes(altered_bytes)
            altered_pin = hashlib.sha256(altered_bytes).hexdigest()
            with self.assertRaisesRegex(ValueError, "changed the approved village runtime mapping"):
                academy_importer.load_map_revision(temp_root, altered_pin)

        runtimes = {record["runtime"] for record in academy_importer.MAP_REVISION_SLOTS.values()}
        payloads = {runtime: f"reviewed:{runtime}".encode("ascii") for runtime in runtimes}
        legacy = {runtime: f"legacy:{runtime}".encode("ascii") for runtime in runtimes}
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            for runtime in runtimes:
                path = temp_root / academy_importer.IMAGE_ROOT / runtime
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(legacy[runtime])
            academy_importer.install_curated_map_bodies(temp_root, payloads, legacy, check_only=False)
            academy_importer.install_curated_map_bodies(temp_root, payloads, legacy, check_only=True)

            # If any destination is unrecognized, preflight must leave the other
            # two exact legacy files untouched rather than partially installing.
            for runtime in runtimes:
                (temp_root / academy_importer.IMAGE_ROOT / runtime).write_bytes(legacy[runtime])
            unknown_runtime = "NH_academy_village_body.png"
            (temp_root / academy_importer.IMAGE_ROOT / unknown_runtime).write_bytes(b"unrecognized")
            with self.assertRaisesRegex(RuntimeError, "unrecognized Academy map-body pixels"):
                academy_importer.install_curated_map_bodies(temp_root, payloads, legacy, check_only=False)
            for runtime in runtimes - {unknown_runtime}:
                self.assertEqual((temp_root / academy_importer.IMAGE_ROOT / runtime).read_bytes(), legacy[runtime])

    def test_village_hall_v2_is_pinned_installed_and_preserves_registration(self):
        revision = academy_importer.load_hall_revision(
            ROOT,
            academy_importer.APPROVED_HALL_REVISION_MANIFEST_SHA256,
        )
        manifest = revision["manifest"]
        self.assertEqual(revision["manifest_sha256"], "80545094b5fccc1e02b0251367ca8388d9d9ab08ad02667b9c2c9ce3a72e1a27")
        self.assertEqual(manifest["revision"], "v2")
        self.assertEqual(manifest["status"], "provisional")
        self.assertEqual(manifest["master"]["sha256"], "bf2071863bdf35c905cfdd5a525b61f298c9cbded01364253559fb64c388ae88")
        self.assertEqual(manifest["prompt"]["sha256"], "7f219149d4d9df1803c2f0931358e3fc2ee41f6cbbba3863ac7c098d02c42bec")
        self.assertEqual(manifest["export"]["sha256"], "117e8ae670dda32e7cee0c76d78d4a12acb3a22bd3467b6b6d2cfd74173763cd")

        # Keep both original artifacts as provenance; only the Academy runtime
        # copy may be replaced by the reviewed, one-frame export.
        self.assertEqual(
            revision["baseline_master_bytes"],
            (ACADEMY_SOURCE / "masters/town/buildings/tbtwhall.png").read_bytes(),
        )
        self.assertEqual(
            revision["baseline_native_bytes"],
            (ACADEMY_SOURCE / academy_importer.HALL_SOURCE_NATIVE).read_bytes(),
        )
        runtime = IMAGES / academy_importer.HALL_RUNTIME_IMAGE
        self.assertEqual(runtime.read_bytes(), revision["export_bytes"])

        alias_record = manifest["preservedRuntime"]["animation"]
        alias_path = IMAGES / alias_record["path"]
        self.assertEqual(hashlib.sha256(alias_path.read_bytes()).hexdigest(), alias_record["sha256"])
        self.assertEqual(
            json.loads(alias_path.read_text(encoding="utf-8"))["images"],
            [{"group": 0, "frame": 0, "file": academy_importer.HALL_RUNTIME_IMAGE}],
        )

        placement = manifest["preservedRuntime"]["corePlacement"]
        core = academy_importer.load_jsonc(ROOT / placement["config"])
        structure = core["tower"]["town"]["structures"][placement["structure"]]
        self.assertEqual([structure[axis] for axis in ("x", "y", "z")], [0, 259, 2])
        for kind, record in manifest["preservedRuntime"]["masks"].items():
            mask_path = IMAGES / record["path"]
            self.assertEqual(hashlib.sha256(mask_path.read_bytes()).hexdigest(), record["sha256"], kind)
            with Image.open(mask_path) as mask:
                self.assertEqual(list(mask.size), record["dimensions"], kind)

    def test_village_hall_revision_rejects_unpinned_inputs(self):
        pin = academy_importer.APPROVED_HALL_REVISION_MANIFEST_SHA256
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            revision_root = temp_root / academy_importer.HALL_REVISION_ROOT
            shutil.copytree(ROOT / academy_importer.HALL_REVISION_ROOT, revision_root)
            for relative in (
                "integration/town-layout.json",
                "masters/town/buildings/tbtwhall.png",
                academy_importer.HALL_SOURCE_NATIVE,
            ):
                source = ROOT / academy_importer.SOURCE_ROOT / relative
                destination = temp_root / academy_importer.SOURCE_ROOT / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
            core_path = temp_root / "config/factions/tower.json"
            core_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "config/factions/tower.json", core_path)

            manifest_path = temp_root / academy_importer.HALL_REVISION_MANIFEST
            original_manifest = manifest_path.read_bytes()
            manifest_path.write_bytes(original_manifest + b" ")
            with self.assertRaisesRegex(RuntimeError, "manifest changed"):
                academy_importer.load_hall_revision(temp_root, pin)
            manifest_path.write_bytes(original_manifest)

            for relative, error in (
                ("village-hall-master.png", "master bytes do not match"),
                ("village-hall.prompt.txt", "prompt bytes do not match"),
                ("exports/village-hall-native.png", "export bytes do not match"),
            ):
                with self.subTest(path=relative):
                    path = revision_root / relative
                    original = path.read_bytes()
                    path.write_bytes(original + b" ")
                    with self.assertRaisesRegex(ValueError, error):
                        academy_importer.load_hall_revision(temp_root, pin)
                    path.write_bytes(original)

    def test_village_hall_install_is_idempotent_and_refuses_unknown_runtime(self):
        revision = academy_importer.load_hall_revision(
            ROOT,
            academy_importer.APPROVED_HALL_REVISION_MANIFEST_SHA256,
        )
        legacy = revision["baseline_native_bytes"]
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            runtime = temp_root / academy_importer.IMAGE_ROOT / academy_importer.HALL_RUNTIME_IMAGE
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(legacy)

            with self.assertRaisesRegex(RuntimeError, "not installed"):
                academy_importer.install_curated_hall(temp_root, revision, legacy, check_only=True)
            self.assertEqual(runtime.read_bytes(), legacy)

            academy_importer.install_curated_hall(temp_root, revision, legacy, check_only=False)
            installed = runtime.read_bytes()
            self.assertEqual(installed, revision["export_bytes"])
            academy_importer.install_curated_hall(temp_root, revision, legacy, check_only=True)
            academy_importer.install_curated_hall(temp_root, revision, legacy, check_only=False)
            self.assertEqual(runtime.read_bytes(), installed)

            runtime.write_bytes(b"unrecognized local Village Hall pixels")
            with self.assertRaisesRegex(RuntimeError, "unrecognized Academy Village Hall pixels"):
                academy_importer.install_curated_hall(temp_root, revision, legacy, check_only=False)
            self.assertEqual(runtime.read_bytes(), b"unrecognized local Village Hall pixels")

    def test_faction_selection_names_stay_white_and_selection_uses_existing_border(self):
        source = OPTIONS_TAB.read_text(encoding="utf-8")
        start = source.index("void OptionsTab::SelectionWindow::genContentFactions()")
        end = source.index("void OptionsTab::SelectionWindow::genContentHeroes()", start)
        faction_renderer = source[start:end]
        self.assertEqual(faction_renderer.count("drawOutlinedText("), 2)
        self.assertEqual(faction_renderer.count("Colors::WHITE"), 2)
        self.assertNotIn("Colors::YELLOW", faction_renderer)
        self.assertIn("lobby/townBorderSmallActivated", faction_renderer)
        self.assertIn("lobby/townBorderBigActivated", faction_renderer)

    def test_siege_uses_direct_images_and_provenance_exceptions_stay_out(self):
        siege_images = sorted(IMAGES.glob("SGTW*.png"))
        self.assertGreater(len(siege_images), 0)
        for image in siege_images:
            self.assertFalse(image.with_suffix(".json").exists(), image.name)
        for original_gate in ("SGTWDRW1.png", "SGTWDRW2.png", "SGTWDRW3.png", "SGTWDRWC.png"):
            self.assertFalse((IMAGES / original_gate).exists(), original_gate)

        excluded = (
            "native/adventure/avctowr0.png",
            "native/adventure/avctowx0.png",
            "native/adventure/avctowz0.png",
            "native/siege/sgtwdrw1.png",
            "native/siege/sgtwdrw2.png",
            "native/siege/sgtwdrw3.png",
            "native/siege/sgtwdrwc.png",
            "native/ui/icons/fort-large-built.png",
            "native/ui/icons/fort-small-built.png",
            "native/ui/icons/village-large-built.png",
            "native/ui/icons/village-small-built.png",
        )
        for relative in excluded:
            self.assertFalse((ACADEMY_SOURCE / relative).exists(), relative)

        validation = read_json("assets/new-horizons/academy/handoff/VALIDATION.json")
        export_validation = read_json("assets/new-horizons/academy/integration/export-validation.json")
        self.assertFalse(validation["VCMITested"])
        self.assertFalse(export_validation["inGameTested"])
        self.assertTrue((ACADEMY_SOURCE / "README.md").is_file())


if __name__ == "__main__":
    unittest.main()
