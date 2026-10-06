#!/usr/bin/env python3
"""Mechanically register the Academy village hall art at native resolution.

The generated master is cropped to its approved alpha bounds and resized into
the original building's visible bounds. No painting or source modification is
performed. Run with --check to verify all saved outputs without rewriting them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw

from import_new_horizons_academy_assets import IMAGE_ROOT, animation_image_path, load_jsonc


ROOT = Path(__file__).resolve().parents[1]
REVISION = ROOT / "assets/new-horizons/academy/hall-revisions/v2"
MASTER_PATH = REVISION / "village-hall-master.png"
ORIGINAL_PATH = ROOT / "assets/new-horizons/academy/native/town/buildings/tbtwhall.png"
EXPORT_PATH = REVISION / "exports/village-hall-native.png"
NATIVE_COMPARISON_PATH = REVISION / "comparisons/village-hall-native-before-after.png"
UPSCALED_COMPARISON_PATH = REVISION / "comparisons/village-hall-nearest-4x-before-after.png"
TOWN_LAYOUT_PATH = ROOT / "assets/new-horizons/academy/integration/town-layout.json"
SCENE_PREVIEW_DIR = ROOT / "build/nh-up241-validation"
SCENE_NATIVE_PATH = SCENE_PREVIEW_DIR / "village-hall-stage-native-registered.png"
SCENE_V2_PATH = SCENE_PREVIEW_DIR / "village-hall-stage-v2-registered.png"
SCENE_LANDSCAPE_PATH = ROOT / IMAGE_ROOT / "NH_academy/town/landscape.png"
NATIVE_HALL_PATH = ROOT / "assets/new-horizons/academy/native/town/buildings/tbtwhall.png"

MASTER_SIZE = (1927, 816)
MASTER_ALPHA_BBOX = (78, 21, 1872, 792)
NATIVE_SIZE = (177, 75)
NATIVE_ALPHA_BBOX = (0, 2, 177, 75)
VISIBLE_SIZE = (177, 73)
VISIBLE_ORIGIN = (0, 2)


def open_rgba(path: Path) -> Image.Image:
	with Image.open(path) as image:
		return image.convert("RGBA")


def checkerboard(size: tuple[int, int], scale: int) -> Image.Image:
	background = Image.new("RGBA", size, (224, 224, 224, 255))
	draw = ImageDraw.Draw(background)
	cell = 8 * scale
	for y in range(0, size[1], cell):
		for x in range(0, size[0], cell):
			if (x // cell + y // cell) % 2:
				draw.rectangle((x, y, min(x + cell - 1, size[0] - 1), min(y + cell - 1, size[1] - 1)), fill=(190, 190, 190, 255))
	return background


def make_comparison(before: Image.Image, after: Image.Image, scale: int) -> Image.Image:
	if scale == 1:
		left = before
		right = after
	else:
		left = before.resize((before.width * scale, before.height * scale), Image.Resampling.NEAREST)
		right = after.resize((after.width * scale, after.height * scale), Image.Resampling.NEAREST)

	gutter = 8 * scale
	canvas = checkerboard((left.width + gutter + right.width, left.height), scale)
	canvas.alpha_composite(left, (0, 0))
	canvas.alpha_composite(right, (left.width + gutter, 0))
	return canvas


def build_expected() -> dict[Path, Image.Image]:
	master = open_rgba(MASTER_PATH)
	original = open_rgba(ORIGINAL_PATH)
	if master.size != MASTER_SIZE:
		raise ValueError(f"Unexpected master size: {master.size}, expected {MASTER_SIZE}")
	if master.getchannel("A").getbbox() != MASTER_ALPHA_BBOX:
		raise ValueError(
			f"Unexpected master alpha bounds: {master.getchannel('A').getbbox()}, "
			f"expected {MASTER_ALPHA_BBOX}"
		)
	if original.size != NATIVE_SIZE:
		raise ValueError(f"Unexpected original size: {original.size}, expected {NATIVE_SIZE}")
	if original.getchannel("A").getbbox() != NATIVE_ALPHA_BBOX:
		raise ValueError(
			f"Unexpected original alpha bounds: {original.getchannel('A').getbbox()}, "
			f"expected {NATIVE_ALPHA_BBOX}"
		)

	registered_art = master.crop(MASTER_ALPHA_BBOX).resize(VISIBLE_SIZE, Image.Resampling.LANCZOS)
	resized_alpha_bbox = registered_art.getchannel("A").getbbox()
	if resized_alpha_bbox is None:
		raise ValueError("The registered master became fully transparent")
	if resized_alpha_bbox != (0, 0, *VISIBLE_SIZE):
		# Downsampling can erase a one-pixel, low-alpha source edge entirely.
		# Fit the surviving alpha bounds to the approved native visible box.
		registered_art = registered_art.crop(resized_alpha_bbox).resize(VISIBLE_SIZE, Image.Resampling.LANCZOS)
	if registered_art.getchannel("A").getbbox() != (0, 0, *VISIBLE_SIZE):
		raise ValueError("Unable to fit registered alpha bounds to the native visible box")
	exported = Image.new("RGBA", NATIVE_SIZE, (0, 0, 0, 0))
	exported.alpha_composite(registered_art, VISIBLE_ORIGIN)
	return {
		EXPORT_PATH: exported,
		NATIVE_COMPARISON_PATH: make_comparison(original, exported, 1),
		UPSCALED_COMPARISON_PATH: make_comparison(original, exported, 4),
	}


def pixel_equal(actual_path: Path, expected: Image.Image) -> bool:
	if not actual_path.is_file():
		return False
	try:
		actual = open_rgba(actual_path)
	except (OSError, ValueError):
		return False
	return actual.size == expected.size and actual.tobytes() == expected.tobytes()


def sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def effective_village_hall_layers() -> list[tuple[int, int, str, int, int, Path]]:
	"""Resolve a registered VillageHall-stage scene using the importer merge order."""
	town_layout = json.loads(TOWN_LAYOUT_PATH.read_text(encoding="utf-8"))
	if town_layout.get("resolution") != [800, 374]:
		raise ValueError(f"Unexpected retained town-layout resolution: {town_layout.get('resolution')}")

	core_tower = load_jsonc(ROOT / "config/factions/tower.json")
	rank_patch = json.loads(
		(ROOT / "Mods/new-horizons/Content/config/factions/towerCreatureRanks.json").read_text(encoding="utf-8")
	)
	art_patch = json.loads(
		(ROOT / "Mods/new-horizons/Content/config/factions/academyArt.json").read_text(encoding="utf-8")
	)
	core_structures = core_tower["tower"]["town"]["structures"]
	rank_structures = rank_patch["core:tower"]["town"].get("structures", {})
	art_structures = art_patch["core:tower"]["town"].get("structures", {})
	stage_structures = {"townHall", "cityHall", "capitol"}

	layers: list[tuple[int, int, str, int, int, Path]] = []
	village_hall_found = False
	for item in town_layout["layout"]:
		structure_id = item["id"]
		if structure_id in stage_structures:
			continue
		structure = {
			**core_structures.get(structure_id, {}),
			**rank_structures.get(structure_id, {}),
			**art_structures.get(structure_id, {}),
		}
		resource = structure.get("animation")
		if not resource:
			raise ValueError(f"No effective animation for town layer {structure_id}")
		layer_path = ROOT / IMAGE_ROOT / animation_image_path(ROOT, resource)
		x = structure.get("x", item["x"])
		y = structure.get("y", item["y"])
		z = structure.get("z", item.get("z", 0))
		layers.append((z, len(layers), structure_id, x, y, layer_path))
		village_hall_found = village_hall_found or structure_id == "villageHall"

	if not village_hall_found:
		structure_id = "villageHall"
		structure = {
			**core_structures.get(structure_id, {}),
			**rank_structures.get(structure_id, {}),
			**art_structures.get(structure_id, {}),
		}
		resource = structure.get("animation")
		if not resource:
			raise ValueError("No effective animation for the VillageHall-stage layer")
		layer_path = ROOT / IMAGE_ROOT / animation_image_path(ROOT, resource)
		layers.append((structure.get("z", 0), len(layers), structure_id, structure["x"], structure["y"], layer_path))

	# Keep the authored roof as its own sorted layer, as the production preview does.
	roof = art_structures["academyRoof"]
	roof_path = ROOT / IMAGE_ROOT / animation_image_path(ROOT, roof["animation"])
	layers.append((roof.get("z", 0), len(layers), "academyRoof", roof["x"], roof["y"], roof_path))
	return layers


def render_registered_village_hall_scene(layers: list[tuple[int, int, str, int, int, Path]], hall: Image.Image) -> Image.Image:
	output = open_rgba(SCENE_LANDSCAPE_PATH)
	if output.size != (800, 374):
		raise ValueError(f"Supplied native town landscape is {output.size}, expected (800, 374)")
	if hall.size != NATIVE_SIZE:
		raise ValueError(f"VillageHall scene layer is {hall.size}, expected {NATIVE_SIZE}")

	for z, sequence, structure_id, x, y, layer_path in sorted(layers):
		layer = hall if structure_id == "villageHall" else open_rgba(layer_path)
		output.alpha_composite(layer, (x, y))
	return output


def write_scene_preview(exported_hall: Image.Image) -> None:
	layers = effective_village_hall_layers()
	native_hall = open_rgba(NATIVE_HALL_PATH)
	if native_hall.size != NATIVE_SIZE or native_hall.getchannel("A").getbbox() != NATIVE_ALPHA_BBOX:
		raise ValueError("Preserved native VillageHall baseline no longer matches its registered 177x75 bounds")
	live_hall = open_rgba(ROOT / IMAGE_ROOT / "NH_academy/town/buildings/tbtwhall.png")
	approved_live_versions = {native_hall.tobytes(), exported_hall.tobytes()}
	if live_hall.size != NATIVE_SIZE or live_hall.tobytes() not in approved_live_versions:
		raise ValueError("Live VillageHall source differs from both reviewed native and v2 art")

	SCENE_PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
	for path, hall in ((SCENE_NATIVE_PATH, native_hall), (SCENE_V2_PATH, exported_hall)):
		scene = render_registered_village_hall_scene(layers, hall)
		scene.save(path, format="PNG", optimize=False)
		print(f"Wrote {path.relative_to(ROOT)} sha256={sha256(path)}")
	print("Scene is a registered VillageHall-stage comparison; it is not an all-built preview or runtime screenshot.")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--check", action="store_true", help="verify saved outputs without rewriting")
	parser.add_argument("--scene-preview", action="store_true", help="write the ignored native/v2 registered VillageHall-stage scene pair")
	args = parser.parse_args()

	expected = build_expected()
	if args.check:
		missing_or_changed = [str(path.relative_to(ROOT)) for path, image in expected.items() if not pixel_equal(path, image)]
		if missing_or_changed:
			print("Missing or changed outputs:")
			for path in missing_or_changed:
				print(f"  {path}")
			return 1
		for path in expected:
			print(f"OK {path.relative_to(ROOT)} sha256={sha256(path)}")
		return 0

	for path, image in expected.items():
		path.parent.mkdir(parents=True, exist_ok=True)
		image.save(path, format="PNG", optimize=False)
		print(f"Wrote {path.relative_to(ROOT)} sha256={sha256(path)}")
	if args.scene_preview:
		write_scene_preview(expected[EXPORT_PATH])
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
