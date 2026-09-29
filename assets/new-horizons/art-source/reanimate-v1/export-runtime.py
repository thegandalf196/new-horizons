#!/usr/bin/env python3
"""Install deterministic Re-animate spell-icon exports and record their hashes."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
	return hashlib.sha256(path.read_bytes()).hexdigest()


def write_unchanged_or_new(path: Path, data: bytes) -> None:
	if path.exists():
		if path.read_bytes() != data:
			raise FileExistsError(f"refusing to replace different runtime art: {path}")
		return
	path.write_bytes(data)


def main() -> None:
	here = Path(__file__).resolve().parent
	root = here.parents[3]
	live = root / "Mods/new-horizons/Images"
	source_runtime = here / "runtime"
	source_runtime.mkdir(exist_ok=True)
	outputs: dict[str, str] = {}
	for size in (44, 32, 30):
		filename = f"NH_spell_reanimate_{size}.png"
		data = (here / f"exports/reanimate/reanimate-{size}.png").read_bytes()
		for path, manifest_key in (
			(source_runtime / filename, f"source/{filename}"),
			(live / filename, f"Mods/{filename}"),
		):
			write_unchanged_or_new(path, data)
			outputs[manifest_key] = sha256(path)
	manifest = {
		"method": "homm3-art high-resolution master, square LANCZOS reductions",
		"status": "provisional; not final user-approved artwork",
		"assets": ["NH_spell_reanimate_44", "NH_spell_reanimate_32", "NH_spell_reanimate_30"],
		"files": outputs,
	}
	manifest_path = here / "runtime-manifest.json"
	manifest_data = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
	write_unchanged_or_new(manifest_path, manifest_data)


if __name__ == "__main__":
	main()
