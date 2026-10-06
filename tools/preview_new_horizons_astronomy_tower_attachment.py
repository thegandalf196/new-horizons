#!/usr/bin/env python3
"""Compose registered native Academy fort-stage previews for Astronomy Tower placement.

This is a visual/check artifact generator only. It combines the current 800x374
town background with the already-authored fortification and Astronomy Tower
sprites; it does not alter or resample any runtime art.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

from import_new_horizons_academy_assets import IMAGE_ROOT, animation_image_path, load_jsonc


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "build/nh-up247-validation"
CORE_TOWER = ROOT / "config/factions/tower.json"
RANK_PATCH = ROOT / "Mods/new-horizons/Content/config/factions/towerCreatureRanks.json"
ART_PATCH = ROOT / "Mods/new-horizons/Content/config/factions/academyArt.json"
STAGE_STRUCTURES = ("fort", "citadel", "castle")
EXPECTED_STAGE_POSITIONS = {
	"fort": (304, 0, -1),
	"citadel": (301, 0, -1),
	"castle": (301, 0, -1),
}
EXPECTED_TOWER_POSITION = (409, 82, 0)


def open_rgba(path: Path) -> Image.Image:
	with Image.open(path) as image:
		return image.convert("RGBA")


def sha256(path: Path) -> str:
	return hashlib.sha256(path.read_bytes()).hexdigest()


def effective_structures() -> tuple[dict, dict]:
	core_tower = load_jsonc(CORE_TOWER)
	rank_patch = json.loads(RANK_PATCH.read_text(encoding="utf-8"))
	art_patch = json.loads(ART_PATCH.read_text(encoding="utf-8"))
	core = core_tower["tower"]["town"]["structures"]
	ranks = rank_patch["core:tower"]["town"].get("structures", {})
	art = art_patch["core:tower"]["town"].get("structures", {})
	effective = {
		name: {**core.get(name, {}), **ranks.get(name, {}), **art.get(name, {})}
		for name in (*STAGE_STRUCTURES, "special2")
	}
	return effective, art.get("special2", {})


def structure_image(structure: dict) -> Image.Image:
	resource = structure.get("animation")
	if not resource:
		raise ValueError("Registered Academy structure has no animation")
	path = ROOT / IMAGE_ROOT / animation_image_path(ROOT, resource)
	return open_rgba(path)


def scene_background() -> Image.Image:
	background = open_rgba(ROOT / IMAGE_ROOT / "NH_academy/town/landscape.png")
	if background.size != (800, 374):
		raise ValueError(f"Expected native 800x374 Academy landscape, got {background.size}")
	return background


def place_structure(scene: Image.Image, image: Image.Image, position: tuple[int, int, int]) -> None:
	x, y, _ = position
	scene.alpha_composite(image, (x, y))


def overlap_count(stage_image: Image.Image, stage_position: tuple[int, int, int], tower_image: Image.Image,
			  tower_position: tuple[int, int, int]) -> int:
	stage_mask = Image.new("L", (800, 374), 0)
	tower_mask = Image.new("L", (800, 374), 0)
	stage_mask.paste(stage_image.getchannel("A"), stage_position[:2])
	tower_mask.paste(tower_image.getchannel("A"), tower_position[:2])
	return sum(ImageChops.multiply(stage_mask, tower_mask).histogram()[1:])


def render_stage(stage_config: dict, tower_config: dict, tower_x: int, *, show_hit_area: bool = False) -> Image.Image:
	background = scene_background()
	stage_image = structure_image(stage_config)
	tower_image = structure_image(tower_config)
	stage_pos = (stage_config["x"], stage_config["y"], stage_config.get("z", 0))
	tower_pos = (tower_x, tower_config["y"], tower_config.get("z", 0))
	layers = [
		(stage_pos[2], 0, stage_image, stage_pos),
		(tower_pos[2], 1, tower_image, tower_pos),
	]
	for _, _, image, position in sorted(layers, key=lambda layer: (layer[0], layer[1])):
		place_structure(background, image, position)

	if show_hit_area:
		art_special2 = json.loads(ART_PATCH.read_text(encoding="utf-8"))["core:tower"]["town"]["structures"]["special2"]
		area_path = ROOT / IMAGE_ROOT / art_special2["area"]
		border_path = ROOT / IMAGE_ROOT / art_special2["border"]
		area = open_rgba(area_path)
		border = open_rgba(border_path)
		if area.size != tower_image.size or border.size != tower_image.size:
			raise ValueError("Astronomy Tower area/border masks must remain aligned with its native sprite")
		# A translucent overlay makes the unchanged area mask's shared anchor visible;
		# the border image is the same authored hover mask used by CBuildingRect.
		area_tint = Image.new("RGBA", area.size, (30, 160, 255, 96))
		area_tint.putalpha(area.getchannel("A").point(lambda value: value * 96 // 255))
		background.alpha_composite(area_tint, tower_pos[:2])
		background.alpha_composite(border, tower_pos[:2])
	return background


def labeled_pair(before: Image.Image, after: Image.Image, stage: str) -> Image.Image:
	label_height = 24
	canvas = Image.new("RGBA", (1600, 374 + label_height), (24, 24, 28, 255))
	draw = ImageDraw.Draw(canvas)
	font = ImageFont.load_default()
	draw.text((8, 5), f"{stage.title()} stage - baseline special2 x=409", fill=(235, 228, 206, 255), font=font)
	draw.text((808, 5), f"{stage.title()} stage - Academy override x={after.info['tower_x']}",
		  fill=(235, 228, 206, 255), font=font)
	canvas.alpha_composite(before, (0, label_height))
	canvas.alpha_composite(after, (800, label_height))
	return canvas


def save_preview(path: Path, image: Image.Image) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	image.save(path, format="PNG", optimize=False)
	try:
		display_path = path.relative_to(ROOT)
	except ValueError:
		display_path = path
	print(f"Wrote {display_path} sha256={sha256(path)}")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--candidate-x", type=int, help="preview a candidate X before editing academyArt.json")
	parser.add_argument("--output-dir", type=Path, default=OUTPUT_DIR)
	args = parser.parse_args()
	output_dir = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir

	structures, art_special2 = effective_structures()
	core_tower = load_jsonc(CORE_TOWER)
	core_structures = core_tower["tower"]["town"]["structures"]
	core_special2 = core_structures["special2"]
	if tuple(core_special2.get(axis, 0) for axis in ("x", "y", "z")) != EXPECTED_TOWER_POSITION:
		raise ValueError("Core Astronomy Tower registration changed; update the reviewed baseline explicitly")
	for stage, expected in EXPECTED_STAGE_POSITIONS.items():
		actual = tuple(core_structures[stage].get(axis, 0) for axis in ("x", "y", "z"))
		if actual != expected:
			raise ValueError(f"Core {stage} registration changed: {actual}, expected {expected}")
	after_x = args.candidate_x if args.candidate_x is not None else art_special2.get("x", core_special2["x"])
	if not (core_special2["x"] - 12 <= after_x < core_special2["x"]):
		raise ValueError(f"Candidate x={after_x} must be a small leftward offset from {core_special2['x']}")
	if art_special2.get("y", core_special2["y"]) != core_special2["y"]:
		raise ValueError("Astronomy Tower Y placement must remain unchanged")
	if art_special2.get("z", core_special2.get("z", 0)) != core_special2.get("z", 0):
		raise ValueError("Astronomy Tower draw order must remain unchanged")

	tower_image = structure_image(structures["special2"])
	area_path = ROOT / IMAGE_ROOT / art_special2["area"]
	border_path = ROOT / IMAGE_ROOT / art_special2["border"]
	area = open_rgba(area_path)
	border = open_rgba(border_path)
	if area.size != tower_image.size or border.size != tower_image.size:
		raise ValueError("Astronomy Tower area/border masks no longer match its sprite dimensions")
	print(f"special2 sprite/masks={tower_image.size}; area={art_special2['area']}; border={art_special2['border']}")

	for stage in STAGE_STRUCTURES:
		stage_config = structures[stage]
		stage_image = structure_image(stage_config)
		stage_position = (stage_config["x"], stage_config["y"], stage_config.get("z", 0))
		tower_position_before = (core_special2["x"], core_special2["y"], core_special2.get("z", 0))
		tower_position_after = (after_x, core_special2["y"], core_special2.get("z", 0))
		before_overlap = overlap_count(stage_image, stage_position, tower_image, tower_position_before)
		after_overlap = overlap_count(stage_image, stage_position, tower_image, tower_position_after)
		print(f"{stage}: opaque sprite overlap {before_overlap} -> {after_overlap} pixels")
		before = render_stage(stage_config, structures["special2"], core_special2["x"])
		after = render_stage(stage_config, structures["special2"], after_x)
		after.info["tower_x"] = after_x
		save_preview(output_dir / f"astronomy-{stage}-before-after.png", labeled_pair(before, after, stage))
		if stage == "fort":
			mask_before = render_stage(stage_config, structures["special2"], core_special2["x"], show_hit_area=True)
			mask_after = render_stage(stage_config, structures["special2"], after_x, show_hit_area=True)
			mask_after.info["tower_x"] = after_x
			save_preview(output_dir / "astronomy-fort-hit-mask-anchor.png",
					labeled_pair(mask_before, mask_after, "fort hit-mask"))

	print("Previews use current 800x374 landscape and native town sprites; no runtime art or masks were modified.")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
