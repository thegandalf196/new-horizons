#!/usr/bin/env python3
"""Export Empower Spell art as four VCMI perk button states."""

from __future__ import annotations

import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path

from PIL import Image, ImageEnhance


def sha256(path: Path) -> str:
	return hashlib.sha256(path.read_bytes()).hexdigest()


def write_unchanged_or_new(path: Path, data: bytes) -> None:
	if path.exists():
		if path.read_bytes() != data:
			raise FileExistsError(f"refusing to replace different runtime art: {path}")
		return
	path.write_bytes(data)


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("--staging-root", type=Path, required=True, help="External authoring mirror; never installs shipping art")
	args = parser.parse_args()
	repository_path = Path(__file__).resolve().parents[4]
	staging = args.staging_root.resolve()
	if staging == repository_path or repository_path in staging.parents:
	    parser.error("Authoring staging must be outside the checkout")
	here = staging / "assets/new-horizons/art-source/empower-spell-v1"
	root = staging
	live = root / "Mods/new-horizons/Images"
	source_runtime = here / "runtime"
	source_runtime.mkdir(exist_ok=True)
	stem = "NH_perk_empower_spell"
	normal = Image.open(here / "exports/empower-spell/empower-spell-44.png").convert("RGBA")
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
		filename = f"{stem}_{state}.png"
		for path, manifest_key in (
			(source_runtime / filename, f"source/{filename}"),
			(live / filename, f"Mods/{filename}"),
		):
			stream = BytesIO()
			image.save(stream, format="PNG")
			write_unchanged_or_new(path, stream.getvalue())
			outputs[manifest_key] = sha256(path)
		frames.append({"group": 0, "frame": frame, "file": filename})

	descriptor_text = json.dumps({"images": frames}, indent=2) + "\n"
	descriptor_data = descriptor_text.encode("utf-8")
	for descriptor, manifest_key in (
		(source_runtime / f"{stem}.json", f"source/{stem}.json"),
		(live / f"{stem}.json", f"Mods/{stem}.json"),
	):
		write_unchanged_or_new(descriptor, descriptor_data)
		outputs[manifest_key] = sha256(descriptor)

	manifest = {
		"method": "homm3-art high-resolution master, LANCZOS reduction, brightness/color-only runtime states",
		"status": "provisional; not final user-approved artwork",
		"assets": [stem],
		"files": outputs,
	}
	manifest_path = here / "runtime-manifest.json"
	manifest_data = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
	write_unchanged_or_new(manifest_path, manifest_data)


if __name__ == "__main__":
	main()
