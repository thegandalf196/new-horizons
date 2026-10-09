#!/usr/bin/env python3
"""Export the provisional Chain Gate icon into VCMI button states."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageEnhance


ASSETS = (("chain-gate", "NH_perk_chain_gate"),)


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
	here = staging / "assets/new-horizons/art-source/chain-gate-v1"
	root = staging
	live = root / "Mods/new-horizons/Images"
	outputs: dict[str, str] = {}

	for slug, stem in ASSETS:
		normal = Image.open(here / "exports" / slug / f"{slug}-44.png").convert("RGBA")
		if normal.size != (44, 44):
			raise ValueError(f"Expected a 44x44 runtime icon: {slug}")
		states = {
			"normal": normal,
			"pressed": ImageEnhance.Brightness(normal).enhance(0.82),
			"disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
			"highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
		}
		frames = []
		for frame, (state, image) in enumerate(states.items()):
			path = live / f"{stem}_{state}.png"
			if path.exists():
				raise FileExistsError(path)
			image.save(path, format="PNG")
			outputs[path.name] = sha256(path)
			frames.append({"group": 0, "frame": frame, "file": path.name})

		descriptor = live / f"{stem}.json"
		if descriptor.exists():
			raise FileExistsError(descriptor)
		descriptor.write_text(json.dumps({"images": frames}, indent=2) + "\n", encoding="utf-8")
		outputs[descriptor.name] = sha256(descriptor)

	manifest = {
		"method": "homm3-art high-resolution master, LANCZOS reduction, brightness/color-only runtime states",
		"status": "provisional; not final user-approved artwork",
		"assets": [stem for _, stem in ASSETS],
		"files": outputs,
	}
	(here / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
	main()
