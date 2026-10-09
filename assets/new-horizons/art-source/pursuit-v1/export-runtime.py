#!/usr/bin/env python3
"""Export the provisional Pursuit icon into VCMI button states."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageEnhance


def sha256(path: Path) -> str:
	return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("--staging-root", type=Path, required=True, help="External authoring mirror; never installs shipping art")
	args = parser.parse_args()
	repository_path = Path(__file__).resolve().parents[4]
	staging = args.staging_root.resolve()
	if staging == repository_path or repository_path in staging.parents:
	    parser.error("Authoring staging must be outside the checkout")
	here = staging / "assets/new-horizons/art-source/pursuit-v1"
	root = staging
	live = root / "Mods/new-horizons/Images"
	stem = "NH_perk_pursuit"
	normal = Image.open(here / "exports/pursuit/pursuit-44.png").convert("RGBA")
	if normal.size != (44, 44):
		raise ValueError("Expected a 44x44 runtime icon")
	states = {
		"normal": normal,
		"pressed": ImageEnhance.Brightness(normal).enhance(0.82),
		"disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
		"highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
	}
	outputs: dict[str, str] = {}
	frames = []
	for frame, (state, image) in enumerate(states.items()):
		path = live / f"{stem}_{state}.png"
		if path.exists():
			with Image.open(path) as existing:
				if existing.convert("RGBA").tobytes() != image.tobytes():
					raise FileExistsError(f"refusing to replace different runtime art: {path}")
		else:
			image.save(path, format="PNG")
		outputs[path.name] = sha256(path)
		frames.append({"group": 0, "frame": frame, "file": path.name})

	descriptor = live / f"{stem}.json"
	descriptor_text = json.dumps({"images": frames}, indent=2) + "\n"
	if descriptor.exists() and descriptor.read_text(encoding="utf-8") != descriptor_text:
		raise FileExistsError(f"refusing to replace different descriptor: {descriptor}")
	if not descriptor.exists():
		descriptor.write_text(descriptor_text, encoding="utf-8")
	outputs[descriptor.name] = sha256(descriptor)

	manifest = {
		"method": "homm3-art high-resolution master, LANCZOS reduction, brightness/color-only runtime states",
		"status": "provisional; not final user-approved artwork",
		"asset_binding": {"perk_id": "new-horizons:offense.pursuit", "image_key": stem},
		"files": outputs,
	}
	(here / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
	main()
