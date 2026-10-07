"""Focused integrity tests for the refreshed private Cabir handoff importer."""

from __future__ import annotations

from io import BytesIO
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import import_new_horizons_cabir_complete_handoff as importer  # noqa: E402
import import_new_horizons_cabir_handoff as prior  # noqa: E402


def _png(size: tuple[int, int], color: tuple[int, int, int, int]) -> bytes:
	image = Image.new("RGBA", size, (0, 0, 0, 0))
	draw = ImageDraw.Draw(image)
	draw.rectangle((max(0, size[0] // 3), max(0, size[1] // 4), min(size[0] - 1, size[0] // 2), min(size[1] - 1, size[1] * 3 // 4)), fill=color)
	stream = BytesIO()
	image.save(stream, format="PNG", optimize=False)
	return stream.getvalue()


def _json(data: bytes) -> dict:
	return json.loads(data)


class CabirCompleteHandoffTest(unittest.TestCase):
	def setUp(self):
		(ROOT / "build").mkdir(exist_ok=True)
		self.temp = tempfile.TemporaryDirectory(prefix="nh-cabir-complete-test-", dir=ROOT / "build")
		self.root = Path(self.temp.name)
		self.package = self.root / "package"
		self.package.mkdir()
		self.prior_outputs = {}
		self._make_refreshed_package()
		self._make_prior_outputs()

	def tearDown(self):
		self.temp.cleanup()

	def _write_package_file(self, relative: str, data: bytes):
		path = self.package / relative
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)
		return data

	def _make_refreshed_package(self):
		state_manifest = {}
		for form_key, form in importer.FORM_SPECS.items():
			for state in form["states"]:
				state_manifest[f"{form_key}/{state.source_folder}"] = {
					"group": state.group,
					"count": state.count,
					"canvas": list(importer.BATTLE_CANVAS),
				}
		for form_key in importer.FORM_SPECS:
			for label in ("turn-right-to-left", "turn-left-to-right"):
				state_manifest[f"{form_key}/{label}"] = {"group": None, "count": 4, "canvas": list(importer.BATTLE_CANVAS)}

		for form_index, (form_key, form) in enumerate(importer.FORM_SPECS.items()):
			for state_index, state in enumerate(form["states"]):
				for frame_index in range(state.count):
					path = f"export/animations/{form['sourceForm']}/{state.source_folder}/frame-{frame_index:02}.png"
					self._write_package_file(path, _png(importer.BATTLE_CANVAS, (40 + form_index, 50 + state_index, frame_index + 1, 255)))

		for form_index, form in enumerate(prior.FORMS.values()):
			form_key = form["package"]
			for frame_index in range(7):
				name = f"frame-{frame_index:02}.png"
				data = _png((128, 112), (30 + form_index, 80, frame_index + 1, 255))
				self._write_package_file(f"approved-inputs/animations/{form_key}/walk/{name}", data)
			for role, filename in form["icons"].items():
				width, height = prior.ICON_SIZES[role]
				data = _png((width, height), (100 + form_index, 30, 60, 255))
				self._write_package_file(f"approved-inputs/icons/{filename}", data)
				self._write_package_file(f"export/icons/{filename}", data)

		for frame_index in range(9):
			filename = f"cabir-fire-angle-{frame_index:02}.png"
			data = _png(prior.PROJECTILE_CANVAS, (90, 70 + frame_index, 40, 255))
			self._write_package_file(f"approved-inputs/projectile/{filename}", data)
			self._write_package_file(f"export/projectile/{filename}", data)

		export_manifest = {
			"status": "Complete requested artwork review candidate; aesthetic approval and runtime integration pending",
			"engineStateGroups": state_manifest,
			"turnContract": "group7 first half; facing flip; group8 locally mirrored second half. Full turn GIFs show world-space result.",
			"mouthCanvasXY": importer.MOUTH_CANVAS,
			"projectileAngleChoices": importer.PROJECTILE_ANGLE_FRAME,
		}
		self._write_package_file("export/manifest.json", json.dumps(export_manifest).encode("utf-8"))
		self._write_package_file("verification.json", json.dumps({"runtimeInstalled": False, "aestheticApproval": "pending"}).encode("utf-8"))

		manifest_files = []
		for path in sorted(self.package.rglob("*")):
			if not path.is_file():
				continue
			data = path.read_bytes()
			manifest_files.append({
				"path": path.relative_to(self.package).as_posix(),
				"bytes": len(data),
				"sha256": hashlib.sha256(data).hexdigest(),
			})
		manifest = {"status": "Private complete artwork review candidate", "files": manifest_files}
		(self.package / "PACKAGE_MANIFEST.json").write_text(json.dumps(manifest), encoding="utf-8")

	def _make_prior_outputs(self):
		for form_key, form in prior.FORMS.items():
			map_descriptor = {
				"basepath": form["mapBase"],
				"sequences": [{"group": 0, "frames": [f"walk/frame-{i:02}.png" for i in range(7)]}],
			}
			path = f"{prior.CONTENT_PREFIX}/sprites/{form['mapDescriptor'].replace('.def', '.json')}"
			self.prior_outputs[path] = (json.dumps(map_descriptor) + "\n").encode("utf-8")
			for frame_index in range(7):
				filename = f"frame-{frame_index:02}.png"
				source = self.package / f"approved-inputs/animations/{form['package']}/walk/{filename}"
				path = f"{prior.IMAGE_PREFIX}/{form['mapBase']}walk/{filename}"
				self.prior_outputs[path] = source.read_bytes()
			for role, source_name in form["icons"].items():
				filename = form["runtimeIcons"][role]
				source = self.package / f"approved-inputs/icons/{source_name}"
				path = f"{prior.IMAGE_PREFIX}/cabir-handoff/{form_key}/icons/{filename}"
				self.prior_outputs[path] = source.read_bytes()

		projectile_images = []
		for frame_index in range(9):
			filename = f"frame-{frame_index:02}.png"
			source = self.package / f"approved-inputs/projectile/cabir-fire-angle-{frame_index:02}.png"
			self.prior_outputs[f"{prior.IMAGE_PREFIX}/{prior.PROJECTILE_BASE}{filename}"] = source.read_bytes()
			projectile_images.append({"group": 0, "frame": frame_index, "file": filename})
		projectile_descriptor = {"basepath": prior.PROJECTILE_BASE, "images": projectile_images}
		path = f"{prior.CONTENT_PREFIX}/sprites/{prior.PROJECTILE_DESCRIPTOR.replace('.def', '.json')}"
		self.prior_outputs[path] = (json.dumps(projectile_descriptor) + "\n").encode("utf-8")

	def _build(self):
		return importer.build_overlay(
			self.package,
			expected_package_sha256=None,
			expected_export_sha256=None,
			expected_package_file_count=None,
			prior_outputs_override=self.prior_outputs,
			prior_metadata_override={"handoffManifestSha256": importer.EXPECTED_PRIOR_HANDOFF_MANIFEST_SHA256},
		)

	def test_actual_groups_are_emitted_without_placeholder_aliases(self):
		outputs, metadata = self._build()
		expected = {
			"cabir": [0, 2, 3, 4, 5, 7, 8, 11, 12, 13, 14, 15, 16, 20, 21, 22],
			"cabir-master": [0, 2, 3, 4, 5, 7, 8, 11, 12, 13, 14, 15, 16, 18, 20, 21, 22],
		}
		for form_key, form in importer.FORM_SPECS.items():
			path = f"{importer.SPRITE_PREFIX}/{form['battleDescriptor'].replace('.def', '.json')}"
			descriptor = _json(outputs[path])
			self.assertEqual([item["group"] for item in descriptor["sequences"]], expected[form_key])
			self.assertTrue(all(item["generateOverlay"] == 1 for item in descriptor["sequences"]))
			self.assertEqual(metadata["creatures"][form_key]["actualGroupCount"], len(expected[form_key]))
			self.assertEqual(metadata["creatures"][form_key]["aliasesEmitted"], False)
			for sequence in descriptor["sequences"]:
				for frame in sequence["frames"]:
					asset_path = f"{importer.IMAGE_PREFIX}/{descriptor['basepath']}{frame}"
					self.assertIn(asset_path, outputs)
		self.assertEqual(metadata["creatures"]["cabir"]["nativeFrameCount"], 77)
		self.assertEqual(metadata["creatures"]["cabir-master"]["nativeFrameCount"], 83)
		self.assertIn("24", metadata["optionalGroupsNotEmitted"])
		self.assertIn("25", metadata["optionalGroupsNotEmitted"])
		self.assertFalse(metadata["runtimeInstalled"])
		self.assertFalse(metadata["gameplayChanged"])

	def test_reused_prior_map_icons_projectile_and_graphics_patch(self):
		outputs, _metadata = self._build()
		for relative, data in self.prior_outputs.items():
			if relative in importer._preserved_prior_paths():
				self.assertEqual(outputs[relative.removeprefix("overlay/")], data)
		patch = _json(outputs[importer.PATCH_NAME])
		self.assertEqual(set(patch), {"creatures"})
		self.assertEqual(set(patch["creatures"]), {"core:gremlin", "core:masterGremlin"})
		for patch_entry in patch["creatures"].values():
			self.assertEqual(patch_entry["remove"], [])
			graphics = patch_entry["set"]
			self.assertEqual(graphics["missile"]["attackClimaxFrame"], 4)
			self.assertEqual(graphics["missile"]["offset"], importer.PROJECTILE_OFFSET)
			self.assertEqual(graphics["missile"]["projectile"], "NH_CabirHandoffFireball.def")
			self.assertEqual(graphics["mapAttackFromLeft"], graphics["mapAttackFromRight"])

	def test_new_native_frames_are_direct_byte_copies(self):
		outputs, metadata = self._build()
		for form_key, form in importer.FORM_SPECS.items():
			for state in form["states"]:
				for index in range(state.count):
					source = f"export/animations/{form['sourceForm']}/{state.source_folder}/frame-{index:02}.png"
					destination = f"{importer.IMAGE_PREFIX}/{form['battleBase']}{state.source_folder}/frame-{index:02}.png"
					self.assertEqual(outputs[destination], (self.package / source).read_bytes())
		self.assertEqual(len(metadata["sourceHashes"]), 160)

	def test_changed_source_pin_fails_closed(self):
		target = self.package / "export/animations/cabir/walk/frame-00.png"
		target.write_bytes(target.read_bytes() + b"tamper")
		with self.assertRaisesRegex(ValueError, "hash or length mismatch"):
			self._build()

	def test_output_write_and_check_are_exact_and_refuse_overwrite(self):
		outputs, _metadata = self._build()
		destination = self.root / "candidate"
		resolved = importer._safe_output(destination)
		importer._check_or_write_outputs(outputs, resolved, check=False)
		importer._check_or_write_outputs(outputs, resolved, check=True)
		with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
			importer._check_or_write_outputs(outputs, resolved, check=False)
		with self.assertRaisesRegex(ValueError, "file inventory differs"):
			(resolved / "extra.txt").write_text("extra")
			importer._check_or_write_outputs(outputs, resolved, check=True)


if __name__ == "__main__":
	unittest.main()
